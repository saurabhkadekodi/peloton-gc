//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// mutate_test.cpp
//
// Identification: tests/executor/mutate_test.cpp
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "backend/catalog/schema.h"
#include "backend/common/value_factory.h"

#include "backend/executor/delete_executor.h"
#include "backend/executor/insert_executor.h"
#include "backend/executor/seq_scan_executor.h"
#include "backend/executor/update_executor.h"
#include "backend/executor/logical_tile_factory.h"
#include "backend/expression/expression_util.h"
#include "backend/expression/abstract_expression.h"
#include "backend/expression/expression.h"
#include "backend/storage/tile_group.h"
#include "backend/storage/table_factory.h"

#include "executor_tests_util.h"
#include "executor/mock_executor.h"
#include "harness.h"

#include <atomic>
#include "../../src/backend/planner/delete_plan.h"
#include "../../src/backend/planner/insert_plan.h"
#include "../../src/backend/planner/seq_scan_plan.h"
#include "../../src/backend/planner/update_plan.h"

using ::testing::NotNull;
using ::testing::Return;

namespace peloton {
namespace test {

//===------------------------------===//
// Utility
//===------------------------------===//

/**
 * Cook a ProjectInfo object from a tuple.
 * Simply use a ConstantValueExpression for each attribute.
 */
planner::ProjectInfo *MakeProjectInfoFromTuple(const storage::Tuple *tuple) {
  planner::ProjectInfo::TargetList target_list;
  planner::ProjectInfo::DirectMapList direct_map_list;

  for (oid_t col_id = START_OID; col_id < tuple->GetColumnCount(); col_id++) {
    auto value = tuple->GetValue(col_id);
    auto expression = expression::ConstantValueFactory(value);
    target_list.emplace_back(col_id, expression);
  }

  return new planner::ProjectInfo(target_list, direct_map_list);
}

//===--------------------------------------------------------------------===//
// Mutator Tests
//===--------------------------------------------------------------------===//

std::atomic<int> tuple_id;
std::atomic<int> delete_tuple_id;

void InsertTuple(storage::DataTable *table) {
  auto &txn_manager = concurrency::TransactionManager::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  for (oid_t tuple_itr = 0; tuple_itr < 10; tuple_itr++) {
    auto tuple = ExecutorTestsUtil::GetTuple(table, ++tuple_id);

    auto project_info = MakeProjectInfoFromTuple(tuple);

    planner::InsertPlan node(table, project_info);
    executor::InsertExecutor executor(&node, context.get());
    executor.Execute();

    tuple->FreeUninlinedData();
    delete tuple;
  }

  txn_manager.CommitTransaction(txn);
}

void UpdateTuple(storage::DataTable *table) {
  auto &txn_manager = concurrency::TransactionManager::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  // Update
  std::vector<oid_t> update_column_ids = {2};
  std::vector<Value> values;
  Value update_val = ValueFactory::GetDoubleValue(23.5);

  planner::ProjectInfo::TargetList target_list;
  planner::ProjectInfo::DirectMapList direct_map_list;
  target_list.emplace_back(2, expression::ConstantValueFactory(update_val));
  std::cout << target_list.at(0).first << std::endl;
  direct_map_list.emplace_back(0, std::pair<oid_t, oid_t>(0, 0));
  direct_map_list.emplace_back(1, std::pair<oid_t, oid_t>(0, 1));
  direct_map_list.emplace_back(3, std::pair<oid_t, oid_t>(0, 3));

  planner::UpdatePlan update_node(
      table, new planner::ProjectInfo(target_list, direct_map_list));
  executor::UpdateExecutor update_executor(&update_node, context.get());

  // Predicate

  // WHERE ATTR_0 < 60
  expression::TupleValueExpression *tup_val_exp =
      new expression::TupleValueExpression(0, 0, std::string("tablename"),
                                           std::string("colname"));
  expression::ConstantValueExpression *const_val_exp =
      new expression::ConstantValueExpression(
          ValueFactory::GetIntegerValue(60));
  auto predicate = new expression::ComparisonExpression<expression::CmpLt>(
      EXPRESSION_TYPE_COMPARE_LT, tup_val_exp, const_val_exp);

  // Seq scan
  std::vector<oid_t> column_ids = {0};
  planner::SeqScanPlan seq_scan_node(table, predicate, column_ids);
  executor::SeqScanExecutor seq_scan_executor(&seq_scan_node, context.get());

  // Parent-Child relationship
  update_node.AddChild(&seq_scan_node);
  update_executor.AddChild(&seq_scan_executor);

  EXPECT_TRUE(update_executor.Init());
  EXPECT_TRUE(update_executor.Execute());

  txn_manager.CommitTransaction(txn);
}

void DeleteTuple(storage::DataTable *table) {
  auto &txn_manager = concurrency::TransactionManager::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  std::vector<storage::Tuple *> tuples;

  // Delete
  planner::DeletePlan delete_node(table, false);
  executor::DeleteExecutor delete_executor(&delete_node, context.get());

  // Predicate

  // WHERE ATTR_0 > 60
  expression::TupleValueExpression *tup_val_exp =
      new expression::TupleValueExpression(0, 0, std::string("tablename"),
                                           std::string("colname"));
  expression::ConstantValueExpression *const_val_exp =
      new expression::ConstantValueExpression(
          ValueFactory::GetIntegerValue(60));
  auto predicate = new expression::ComparisonExpression<expression::CmpGt>(
      EXPRESSION_TYPE_COMPARE_GT, tup_val_exp, const_val_exp);

  // Seq scan
  std::vector<oid_t> column_ids = {0};
  planner::SeqScanPlan seq_scan_node(table, predicate, column_ids);
  executor::SeqScanExecutor seq_scan_executor(&seq_scan_node, context.get());

  // Parent-Child relationship
  delete_node.AddChild(&seq_scan_node);
  delete_executor.AddChild(&seq_scan_executor);

  EXPECT_TRUE(delete_executor.Init());
  EXPECT_TRUE(delete_executor.Execute());
  EXPECT_TRUE(delete_executor.Execute());

  txn_manager.CommitTransaction(txn);
}

TEST(MutateTests, StressTests) {
  auto &txn_manager = concurrency::TransactionManager::GetInstance();
  auto txn = txn_manager.BeginTransaction();

  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  // Create insert node for this test.
  storage::DataTable *table = ExecutorTestsUtil::CreateTable();

  // Pass through insert executor.
  storage::Tuple *tuple;
  tuple = ExecutorTestsUtil::GetNullTuple(table);

  auto project_info = MakeProjectInfoFromTuple(tuple);

  planner::InsertPlan node(table, project_info);
  executor::InsertExecutor executor(&node, context.get());

  try {
    executor.Execute();
  } catch (ConstraintException &ce) {
    std::cout << ce.what();
  }

  tuple->FreeUninlinedData();
  delete tuple;

  tuple = ExecutorTestsUtil::GetTuple(table, ++tuple_id);
  project_info = MakeProjectInfoFromTuple(tuple);
  planner::InsertPlan node2(table, project_info);
  executor::InsertExecutor executor2(&node2, context.get());
  executor2.Execute();

  try {
    executor2.Execute();
  } catch (ConstraintException &ce) {
    std::cout << ce.what();
  }

  tuple->FreeUninlinedData();
  delete tuple;

  txn_manager.CommitTransaction(txn);

  std::cout << "Start tests \n";

  LaunchParallelTest(1, InsertTuple, table);
  // std::cout << (*table);

  std::cout << "---------------------------------------------\n";

  // LaunchParallelTest(1, UpdateTuple, table);
  // std::cout << (*table);

  std::cout << "---------------------------------------------\n";

  LaunchParallelTest(1, DeleteTuple, table);
  // std::cout << (*table);

  // PRIMARY KEY
  std::vector<catalog::Column> columns;

  columns.push_back(ExecutorTestsUtil::GetColumnInfo(0));
  catalog::Schema *key_schema = new catalog::Schema(columns);
  storage::Tuple *key1 = new storage::Tuple(key_schema, true);
  storage::Tuple *key2 = new storage::Tuple(key_schema, true);

  key1->SetValue(0, ValueFactory::GetIntegerValue(10));
  key2->SetValue(0, ValueFactory::GetIntegerValue(100));

  delete key1;
  delete key2;
  delete key_schema;

  // SEC KEY
  columns.clear();
  columns.push_back(ExecutorTestsUtil::GetColumnInfo(0));
  columns.push_back(ExecutorTestsUtil::GetColumnInfo(1));
  key_schema = new catalog::Schema(columns);

  storage::Tuple *key3 = new storage::Tuple(key_schema, true);
  storage::Tuple *key4 = new storage::Tuple(key_schema, true);

  key3->SetValue(0, ValueFactory::GetIntegerValue(10));
  key3->SetValue(1, ValueFactory::GetIntegerValue(11));
  key4->SetValue(0, ValueFactory::GetIntegerValue(100));
  key4->SetValue(1, ValueFactory::GetIntegerValue(101));

  delete key3;
  delete key4;
  delete key_schema;

  delete table;
}

// Insert a logical tile into a table
TEST(MutateTests, InsertTest) {
  auto &txn_manager = concurrency::TransactionManager::GetInstance();
  auto txn = txn_manager.BeginTransaction();
  std::unique_ptr<executor::ExecutorContext> context(
      new executor::ExecutorContext(txn));

  // We are going to insert a tile group into a table in this test
  std::unique_ptr<storage::DataTable> source_data_table(
      ExecutorTestsUtil::CreateAndPopulateTable());
  std::unique_ptr<storage::DataTable> dest_data_table(
      ExecutorTestsUtil::CreateTable());
  const std::vector<storage::Tuple *> tuples;

  EXPECT_EQ(source_data_table->GetTileGroupCount(), 3);
  EXPECT_EQ(dest_data_table->GetTileGroupCount(), 1);

  planner::InsertPlan node(dest_data_table.get(), nullptr);
  executor::InsertExecutor executor(&node, context.get());

  MockExecutor child_executor;
  executor.AddChild(&child_executor);

  // Uneventful init...
  EXPECT_CALL(child_executor, DInit()).WillOnce(Return(true));

  // Will return one tile.
  EXPECT_CALL(child_executor, DExecute())
      .WillOnce(Return(true))
      .WillOnce(Return(false));

  auto physical_tile = source_data_table->GetTileGroup(0)->GetTile(0);
  std::vector<storage::Tile *> physical_tiles;
  physical_tiles.push_back(physical_tile);

  std::unique_ptr<executor::LogicalTile> source_logical_tile1(
      executor::LogicalTileFactory::WrapTiles(physical_tiles, false));

  EXPECT_CALL(child_executor, GetOutput())
      .WillOnce(Return(source_logical_tile1.release()));

  EXPECT_TRUE(executor.Init());

  EXPECT_TRUE(executor.Execute());
  EXPECT_FALSE(executor.Execute());

  txn_manager.CommitTransaction(txn);

  // We have inserted all the tuples in this logical tile
  EXPECT_EQ(dest_data_table->GetTileGroupCount(), 1);
}

}  // namespace test
}  // namespace peloton
