#include "duckdb/storage/table/table_statistics.hpp"
#include "duckdb/storage/table/persistent_table_data.hpp"

namespace duckdb {

void TableStatistics::Initialize(const vector<LogicalType> &types, PersistentTableData &data) {
	D_ASSERT(Empty());

	column_stats = std::move(data.table_stats.column_stats);
	if (column_stats.size() != types.size()) { // LCOV_EXCL_START
		throw IOException("Table statistics column count is not aligned with table column count. Corrupt file?");
	} // LCOV_EXCL_STOP
}

void TableStatistics::InitializeEmpty(const vector<LogicalType> &types) {
	D_ASSERT(Empty());

	for (auto &type : types) {
		column_stats.push_back(ColumnStatistics::CreateEmptyStats(type));
	}
}

void TableStatistics::InitializeAddColumn(TableStatistics &parent, const LogicalType &new_column_type) {
	D_ASSERT(Empty());

	lock_guard<mutex> stats_lock(parent.stats_lock);
	for (idx_t i = 0; i < parent.column_stats.size(); i++) {
		column_stats.push_back(parent.column_stats[i]);
	}
	column_stats.push_back(ColumnStatistics::CreateEmptyStats(new_column_type));
}

void TableStatistics::InitializeRemoveColumn(TableStatistics &parent, idx_t removed_column) {
	D_ASSERT(Empty());

	lock_guard<mutex> stats_lock(parent.stats_lock);
	for (idx_t i = 0; i < parent.column_stats.size(); i++) {
		if (i != removed_column) {
			column_stats.push_back(parent.column_stats[i]);
		}
	}
}

void TableStatistics::InitializeAlterType(TableStatistics &parent, idx_t changed_idx, const LogicalType &new_type) {
	D_ASSERT(Empty());

	lock_guard<mutex> stats_lock(parent.stats_lock);
	for (idx_t i = 0; i < parent.column_stats.size(); i++) {
		if (i == changed_idx) {
			column_stats.push_back(ColumnStatistics::CreateEmptyStats(new_type));
		} else {
			column_stats.push_back(parent.column_stats[i]);
		}
	}
}

void TableStatistics::InitializeAddConstraint(TableStatistics &parent) {
	D_ASSERT(Empty());

	lock_guard<mutex> stats_lock(parent.stats_lock);
	for (idx_t i = 0; i < parent.column_stats.size(); i++) {
		column_stats.push_back(parent.column_stats[i]);
	}
}

void TableStatistics::MergeStats(TableStatistics &other) {
	auto l = GetLock();
	D_ASSERT(column_stats.size() == other.column_stats.size());
	for (idx_t i = 0; i < column_stats.size(); i++) {
		column_stats[i]->Merge(*other.column_stats[i]);
	}
}

void TableStatistics::MergeStats(idx_t i, BaseStatistics &stats) {
	auto l = GetLock();
	MergeStats(*l, i, stats);
}

void TableStatistics::MergeStats(TableStatisticsLock &lock, idx_t i, BaseStatistics &stats) {
	column_stats[i]->Statistics().Merge(stats);
}

ColumnStatistics &TableStatistics::GetStats(idx_t i) {
	return *column_stats[i];
}

unique_ptr<BaseStatistics> TableStatistics::CopyStats(idx_t i) {
	lock_guard<mutex> l(stats_lock);
	auto result = column_stats[i]->Statistics().Copy();
	if (column_stats[i]->HasDistinctStats()) {
		result.SetDistinctCount(column_stats[i]->DistinctStats().GetCount());
	}
	return result.ToUnique();
}

void TableStatistics::CopyStats(TableStatistics &other) {
	for (auto &stats : column_stats) {
		other.column_stats.push_back(stats->Copy());
	}
}

void TableStatistics::Serialize(Serializer &serializer) {
	for (auto &stats : column_stats) {
		stats->Serialize(serializer);
	}
}

void TableStatistics::Deserialize(Deserializer &source, ColumnList &columns) {
	for (auto &col : columns.Physical()) {
		auto stats = ColumnStatistics::Deserialize(source, col.GetType());
		column_stats.push_back(std::move(stats));
	}
}

unique_ptr<TableStatisticsLock> TableStatistics::GetLock() {
	return make_unique<TableStatisticsLock>(stats_lock);
}

bool TableStatistics::Empty() {
	return column_stats.empty();
}

} // namespace duckdb
