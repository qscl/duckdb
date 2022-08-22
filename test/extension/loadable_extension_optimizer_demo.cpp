#define DUCKDB_EXTENSION_MAIN
#include "duckdb.hpp"
#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/common/field_writer.hpp"
#include "duckdb/common/serializer/buffered_deserializer.hpp"
#include "duckdb/planner/operator/logical_chunk_get.hpp"

using namespace duckdb;

// whatever
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

class WaggleExtension : public OptimizerExtension {
public:
	WaggleExtension() {
		optimize_function = WaggleOptimizeFunction;
	}

	static bool HasParquetScan(LogicalOperator &op) {
		if (op.type == LogicalOperatorType::LOGICAL_GET) {
			auto &get = (LogicalGet &)op;
			return get.function.name == "parquet_scan";
		}
		for (auto &child : op.children) {
			if (HasParquetScan(*child)) {
				return true;
			}
		}
		return false;
	}

	static void WaggleOptimizeFunction(ClientContext &context, OptimizerExtensionInfo *info,
	                                   unique_ptr<LogicalOperator> &plan) {
		if (!HasParquetScan(*plan)) {
			return;
		}
		// rpc

		Value host, port;
		if (!context.TryGetCurrentSetting("waggle_location_host", host) ||
		    !context.TryGetCurrentSetting("waggle_location_port", port)) {
			throw InvalidInputException("Need the parameters damnit");
		}

		// socket create and verification
		auto sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sockfd == -1) {
			throw InternalException("Failed to create socket");
		}

		struct sockaddr_in servaddr;
		bzero(&servaddr, sizeof(servaddr));
		// assign IP, PORT
		servaddr.sin_family = AF_INET;
		auto host_string = host.ToString();
		servaddr.sin_addr.s_addr = inet_addr(host_string.c_str());
		servaddr.sin_port = htons(port.GetValue<int32_t>());

		// connect the client socket to server socket
		if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
			throw IOException("Failed to connect socket %s", string(strerror(errno)));
		}

		BufferedSerializer serializer;
		plan->Serialize(serializer);
		auto data = serializer.GetData();

		ssize_t len = data.size;
		D_ASSERT(write(sockfd, &len, sizeof(idx_t)) == sizeof(idx_t));
		D_ASSERT(write(sockfd, data.data.get(), len) == len);

		auto chunk_collection = make_unique<ChunkCollection>(Allocator::DefaultAllocator());
		idx_t n_chunks;
		D_ASSERT(read(sockfd, &n_chunks, sizeof(idx_t)) == sizeof(idx_t));
		for (idx_t i = 0; i < n_chunks; i++) {
			ssize_t chunk_len;
			D_ASSERT(read(sockfd, &chunk_len, sizeof(idx_t)) == sizeof(idx_t));
			auto buffer = malloc(chunk_len);
			D_ASSERT(buffer);
			D_ASSERT(read(sockfd, buffer, chunk_len) == chunk_len);
			BufferedDeserializer deserializer((data_ptr_t)buffer, chunk_len);
			DataChunk chunk;
			chunk.Deserialize(deserializer);
			chunk_collection->Append(chunk);
			free(buffer);
		}

		auto types = chunk_collection->Types();
		plan = make_unique<LogicalChunkGet>(0, types, move(chunk_collection));

		len = 0;
		(void)len;
		D_ASSERT(write(sockfd, &len, sizeof(idx_t)) == sizeof(idx_t));
		// close the socket
		close(sockfd);
	}
};

//===--------------------------------------------------------------------===//
// Extension load + setup
//===--------------------------------------------------------------------===//
extern "C" {
DUCKDB_EXTENSION_API void loadable_extension_optimizer_demo_init(duckdb::DatabaseInstance &db) {
	Connection con(db);

	// add a parser extension
	auto &config = DBConfig::GetConfig(db);
	config.optimizer_extensions.push_back(WaggleExtension());
	config.AddExtensionOption("waggle_location_host", "host for remote callback", LogicalType::VARCHAR);
	config.AddExtensionOption("waggle_location_port", "port for remote callback", LogicalType::INTEGER);
}

DUCKDB_EXTENSION_API const char *loadable_extension_optimizer_demo_version() {
	return DuckDB::LibraryVersion();
}
}