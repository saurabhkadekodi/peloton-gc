/*-------------------------------------------------------------------------
 *
 * tile_group_test.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/test/tile_group_test.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "gtest/gtest.h"

#include "backend/concurrency/transaction.h"
#include "backend/storage/tile_group.h"
#include "backend/storage/tile_group_factory.h"

#include "harness.h"

namespace peloton {
namespace test {

//===--------------------------------------------------------------------===//
// Tile Group Tests
//===--------------------------------------------------------------------===//

TEST(TileGroupTests, BasicTest) {

    std::vector<catalog::ColumnInfo> columns;
    std::vector<std::string> tile_column_names;
    std::vector<std::vector<std::string> > column_names;
    std::vector<catalog::Schema> schemas;

    // SCHEMA

    catalog::ColumnInfo column1(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "A", true);
    catalog::ColumnInfo column2(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "B", true);
    catalog::ColumnInfo column3(VALUE_TYPE_TINYINT, GetTypeSize(VALUE_TYPE_TINYINT), "C", true);
    catalog::ColumnInfo column4(VALUE_TYPE_VARCHAR, 25, "D", false);

    columns.push_back(column1);
    columns.push_back(column2);

    catalog::Schema *schema1 = new catalog::Schema(columns);
    schemas.push_back(*schema1);

    columns.clear();
    columns.push_back(column3);
    columns.push_back(column4);

    catalog::Schema *schema2 = new catalog::Schema(columns);
    schemas.push_back(*schema2);

    catalog::Schema *schema = catalog::Schema::AppendSchema(schema1, schema2);

    // TILES

    tile_column_names.push_back("COL 1");
    tile_column_names.push_back("COL 2");

    column_names.push_back(tile_column_names);

    tile_column_names.clear();
    tile_column_names.push_back("COL 3");
    tile_column_names.push_back("COL 4");

    column_names.push_back(tile_column_names);

    // TILE GROUP
    storage::AbstractBackend *backend = new storage::VMBackend();

    storage::TileGroup *tile_group = storage::TileGroupFactory::GetTileGroup(INVALID_OID, INVALID_OID, INVALID_OID,
                                     nullptr, backend, schemas, 4);

    // TUPLES

    storage::Tuple *tuple1 = new storage::Tuple(schema, true);
    storage::Tuple *tuple2 = new storage::Tuple(schema, true);

    tuple1->SetValue(0, ValueFactory::GetIntegerValue(1));
    tuple1->SetValue(1, ValueFactory::GetIntegerValue(1));
    tuple1->SetValue(2, ValueFactory::GetTinyIntValue(1));
    tuple1->SetValue(3, ValueFactory::GetStringValue("tuple 1", tile_group->GetTilePool(1)));

    tuple2->SetValue(0, ValueFactory::GetIntegerValue(2));
    tuple2->SetValue(1, ValueFactory::GetIntegerValue(2));
    tuple2->SetValue(2, ValueFactory::GetTinyIntValue(2));
    tuple2->SetValue(3, ValueFactory::GetStringValue("tuple 2", tile_group->GetTilePool(1)));

    // TRANSACTION

    auto& txn_manager = concurrency::TransactionManager::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    txn_id_t txn_id = txn->GetTransactionId();

    EXPECT_EQ(0, tile_group->GetActiveTupleCount());

    auto tuple_slot = tile_group->InsertTuple(txn_id, tuple1);
    tile_group->CommitInsertedTuple(txn_id, tuple_slot);

    tuple_slot =tile_group->InsertTuple(txn_id, tuple2);
    tile_group->CommitInsertedTuple(txn_id, tuple_slot);

    tuple_slot = tile_group->InsertTuple(txn_id, tuple1);
    tile_group->CommitInsertedTuple(txn_id, tuple_slot);

    EXPECT_EQ(3, tile_group->GetActiveTupleCount());

    storage::TileGroupHeader *header = tile_group->GetHeader();

    ItemPointer item(50, 60);

    header->SetPrevItemPointer(2, item);

    txn_manager.CommitTransaction(txn);
    txn_manager.EndTransaction(txn);

    delete tuple1;
    delete tuple2;
    delete schema;

    delete tile_group;
    delete backend;
    delete schema1;
    delete schema2;
}

void TileGroupInsert(storage::TileGroup *tile_group, catalog::Schema *schema) {

    uint64_t thread_id = GetThreadId();

    storage::Tuple *tuple = new storage::Tuple(schema, true);
    auto& txn_manager = concurrency::TransactionManager::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    txn_id_t txn_id = txn->GetTransactionId();

    tuple->SetValue(0, ValueFactory::GetIntegerValue(1));
    tuple->SetValue(1, ValueFactory::GetIntegerValue(1));
    tuple->SetValue(2, ValueFactory::GetTinyIntValue(1));
    tuple->SetValue(3, ValueFactory::GetStringValue("thread " + std::to_string(thread_id), tile_group->GetTilePool(1)));

    for(int insert_itr = 0 ; insert_itr < 1000 ; insert_itr++) {
        auto tuple_slot = tile_group->InsertTuple(txn_id, tuple);
        tile_group->CommitInsertedTuple(txn_id, tuple_slot);
    }

    txn_manager.CommitTransaction(txn);
    txn_manager.EndTransaction(txn);

    delete tuple;
}

TEST(TileGroupTests, StressTest) {

    std::vector<catalog::ColumnInfo> columns;
    std::vector<std::string> tile_column_names;
    std::vector<std::vector<std::string> > column_names;
    std::vector<catalog::Schema> schemas;

    // SCHEMA
    catalog::ColumnInfo column1(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "A", true);
    catalog::ColumnInfo column2(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "B", true);
    catalog::ColumnInfo column3(VALUE_TYPE_TINYINT, GetTypeSize(VALUE_TYPE_TINYINT), "C", true);
    catalog::ColumnInfo column4(VALUE_TYPE_VARCHAR, 50, "D", false);

    columns.push_back(column1);
    columns.push_back(column2);

    catalog::Schema *schema1 = new catalog::Schema(columns);
    schemas.push_back(*schema1);

    columns.clear();
    columns.push_back(column3);
    columns.push_back(column4);

    catalog::Schema *schema2 = new catalog::Schema(columns);
    schemas.push_back(*schema2);

    catalog::Schema *schema = catalog::Schema::AppendSchema(schema1, schema2);

    // TILES
    tile_column_names.push_back("COL 1");
    tile_column_names.push_back("COL 2");
    column_names.push_back(tile_column_names);

    tile_column_names.clear();
    tile_column_names.push_back("COL 3");
    tile_column_names.push_back("COL 4");
    column_names.push_back(tile_column_names);

    // TILE GROUP

    storage::AbstractBackend *backend = new storage::VMBackend();

    storage::TileGroup *tile_group = storage::TileGroupFactory::GetTileGroup(INVALID_OID, INVALID_OID, INVALID_OID,
                                     nullptr, backend, schemas, 10000);

    LaunchParallelTest(6, TileGroupInsert, tile_group, schema);

    EXPECT_EQ(6000, tile_group->GetActiveTupleCount());

    delete tile_group;
    delete backend;
    delete schema1;
    delete schema2;
    delete schema;
}

TEST(TileGroupTests, MVCCInsert) {

    std::vector<catalog::ColumnInfo> columns;
    std::vector<std::string> tile_column_names;
    std::vector<std::vector<std::string> > column_names;
    std::vector<catalog::Schema> schemas;

    // SCHEMA
    catalog::ColumnInfo column1(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "A", true);
    catalog::ColumnInfo column2(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "B", true);
    catalog::ColumnInfo column3(VALUE_TYPE_TINYINT, GetTypeSize(VALUE_TYPE_TINYINT), "C", true);
    catalog::ColumnInfo column4(VALUE_TYPE_VARCHAR, 50, "D", false);

    columns.push_back(column1);
    columns.push_back(column2);

    catalog::Schema *schema1 = new catalog::Schema(columns);
    schemas.push_back(*schema1);

    columns.clear();
    columns.push_back(column3);
    columns.push_back(column4);

    catalog::Schema *schema2 = new catalog::Schema(columns);
    schemas.push_back(*schema2);

    catalog::Schema *schema = catalog::Schema::AppendSchema(schema1, schema2);

    // TILES
    tile_column_names.push_back("COL 1");
    tile_column_names.push_back("COL 2");
    column_names.push_back(tile_column_names);

    tile_column_names.clear();
    tile_column_names.push_back("COL 3");
    tile_column_names.push_back("COL 4");
    column_names.push_back(tile_column_names);

    // TILE GROUP
    storage::AbstractBackend *backend = new storage::VMBackend();

    storage::TileGroup *tile_group = storage::TileGroupFactory::GetTileGroup(INVALID_OID, INVALID_OID, INVALID_OID,
                                     nullptr, backend, schemas, 3);

    storage::Tuple *tuple = new storage::Tuple(schema, true);

    tuple->SetValue(0, ValueFactory::GetIntegerValue(1));
    tuple->SetValue(1, ValueFactory::GetIntegerValue(1));
    tuple->SetValue(2, ValueFactory::GetTinyIntValue(1));
    tuple->SetValue(3, ValueFactory::GetStringValue("abc", tile_group->GetTilePool(1)));

    oid_t tuple_slot_id = INVALID_OID;

    auto& txn_manager = concurrency::TransactionManager::GetInstance();
    auto txn = txn_manager.BeginTransaction();
    txn_id_t txn_id1 = txn->GetTransactionId();
    cid_t cid1 = txn->GetLastCommitId();

    tuple->SetValue(2, ValueFactory::GetIntegerValue(0));
    tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple);
    EXPECT_EQ(0, tuple_slot_id);

    tuple->SetValue(2, ValueFactory::GetIntegerValue(1));
    tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple);
    EXPECT_EQ(1, tuple_slot_id);

    tuple->SetValue(2, ValueFactory::GetIntegerValue(2));
    tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple);
    EXPECT_EQ(2, tuple_slot_id);

    tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple);
    EXPECT_EQ(INVALID_OID, tuple_slot_id);

    storage::TileGroupHeader *header = tile_group->GetHeader();

    // SELECT

    storage::Tuple *result = nullptr;
    result = tile_group->SelectTuple(1, 1);
    EXPECT_NE(result, nullptr);
    delete result;

    header->SetBeginCommitId(0, cid1);
    header->SetBeginCommitId(2, cid1);

    result = tile_group->SelectTuple(1, 1);
    EXPECT_NE(result, nullptr);
    delete result;

    result = tile_group->SelectTuple(1, 0);
    EXPECT_NE(result, nullptr);
    delete result;

    txn_manager.CommitTransaction(txn);
    txn_manager.EndTransaction(txn);

    // DELETE
    auto txn2 = txn_manager.BeginTransaction();
    cid_t cid2 = txn2->GetLastCommitId();

    tile_group->DeleteTuple(cid2, 2);

    txn_manager.CommitTransaction(txn2);
    txn_manager.EndTransaction(txn2);

    delete tuple;
    delete schema;
    delete tile_group;
    delete backend;

    delete schema1;
    delete schema2;
}

// TODO: Peloton Changes
TEST(TileGroupTests, TileCopyTest) {

	std::vector<catalog::ColumnInfo> columns;
	std::vector<std::string> tile_column_names;
	std::vector<std::vector<std::string> > column_names;
	std::vector<catalog::Schema> schemas;


	catalog::ColumnInfo column1(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "A", true);
	catalog::ColumnInfo column2(VALUE_TYPE_INTEGER, GetTypeSize(VALUE_TYPE_INTEGER), "B", true);
	catalog::ColumnInfo column3(VALUE_TYPE_TINYINT, GetTypeSize(VALUE_TYPE_TINYINT), "C", true);
	catalog::ColumnInfo column4(VALUE_TYPE_VARCHAR, 25, "D", false);
	catalog::ColumnInfo column5(VALUE_TYPE_VARCHAR, 25, "E", false);

	columns.push_back(column1);
	columns.push_back(column2);
	columns.push_back(column3);
	columns.push_back(column4);
	columns.push_back(column5);


	catalog::Schema *schema = new catalog::Schema(columns);
	schemas.push_back(*schema);


	tile_column_names.push_back("COL 1");
	tile_column_names.push_back("COL 2");
	tile_column_names.push_back("COL 3");
	tile_column_names.push_back("COL 4");
	tile_column_names.push_back("COL 5");

    column_names.push_back(tile_column_names);


	const int tuple_count = 4;


    storage::AbstractBackend *backend = new storage::VMBackend();
    storage::TileGroup *tile_group = storage::TileGroupFactory::GetTileGroup(INVALID_OID, INVALID_OID, INVALID_OID,
                                     nullptr, backend, schemas, tuple_count);
    storage::TileGroupHeader *tile_group_header = tile_group->GetHeader();

    storage::Tile *tile = storage::TileFactory::GetTile(INVALID_OID, INVALID_OID, INVALID_OID, INVALID_OID,
    												tile_group_header, backend, *schema, nullptr, tuple_count);


    auto& txn_manager = concurrency::TransactionManager::GetInstance();
	auto txn = txn_manager.BeginTransaction();
	txn_id_t txn_id1 = txn->GetTransactionId();
	oid_t tuple_slot_id = INVALID_OID;


    storage::Tuple *tuple1 = new storage::Tuple(schema, true);
	storage::Tuple *tuple2 = new storage::Tuple(schema, true);
	storage::Tuple *tuple3 = new storage::Tuple(schema, true);

	tuple1->SetValue(0, ValueFactory::GetIntegerValue(1));
	tuple1->SetValue(1, ValueFactory::GetIntegerValue(1));
	tuple1->SetValue(2, ValueFactory::GetTinyIntValue(1));
	tuple1->SetValue(3, ValueFactory::GetStringValue("vivek sengupta", tile->GetPool()));
	tuple1->SetValue(4, ValueFactory::GetStringValue("vivek sengupta again", tile->GetPool()));

	tuple2->SetValue(0, ValueFactory::GetIntegerValue(2));
	tuple2->SetValue(1, ValueFactory::GetIntegerValue(2));
	tuple2->SetValue(2, ValueFactory::GetTinyIntValue(2));
	tuple2->SetValue(3, ValueFactory::GetStringValue("ming fang", tile->GetPool()));
	tuple2->SetValue(4, ValueFactory::GetStringValue("ming fang again", tile->GetPool()));

	tuple3->SetValue(0, ValueFactory::GetIntegerValue(3));
	tuple3->SetValue(1, ValueFactory::GetIntegerValue(3));
	tuple3->SetValue(2, ValueFactory::GetTinyIntValue(3));
	tuple3->SetValue(3, ValueFactory::GetStringValue("jinwoong kim", tile->GetPool()));
	tuple3->SetValue(4, ValueFactory::GetStringValue("jinwoong kim again", tile->GetPool()));

	tile->InsertTuple(0, tuple1);
	tile->InsertTuple(1, tuple2);
	tile->InsertTuple(2, tuple3);

	tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple1);
	EXPECT_EQ(0, tuple_slot_id);
	tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple2);
	EXPECT_EQ(1, tuple_slot_id);
	tuple_slot_id = tile_group->InsertTuple(txn_id1, tuple3);
	EXPECT_EQ(2, tuple_slot_id);


	txn_manager.CommitTransaction(txn);
	txn_manager.EndTransaction(txn);



	/*
	 * Print original tile, make a copy, print copied tile
	 */
/*
	std::cout << std::endl;
	std::cout << "\t Original Tile Details ..." << std::endl << std::endl;
	std::cout << (*tile);
*/

	const catalog::Schema *old_schema = tile->GetSchema();
	const catalog::Schema *new_schema = old_schema;
	storage::AbstractBackend *new_backend = new storage::VMBackend();
	storage::Tile *new_tile = tile->CopyTile(new_backend);

/*
	std::cout << std::endl;
	std::cout << "\t Copied Tile Details ..." << std::endl << std::endl;
	std::cout << (*new_tile);
*/


	/*
	 * Test for equality of old and new tile data
	 */
	int uninlined_col_index;
	Value uninlined_col_value, new_uninlined_col_value;
	size_t uninlined_col_object_len, new_uninlined_col_object_len;
	unsigned char *uninlined_col_object_ptr, *new_uninlined_col_object_ptr;

	bool intended_behavior = true;

	peloton::Pool *old_pool = tile->GetPool();
	peloton::Pool *new_pool = new_tile->GetPool();

	int is_pool_same = old_pool == new_pool;
	if(is_pool_same)
		intended_behavior = false;

	for(int col_itr=0; col_itr<2; col_itr++) {

		uninlined_col_index = new_schema->GetUninlinedColumnIndex(col_itr);
		uint16_t new_tile_active_tuple_count = new_tile->GetActiveTupleCount();

		for(int tup_itr=0; tup_itr<new_tile_active_tuple_count; tup_itr++) {

			uninlined_col_value = tile->GetValue(tup_itr,uninlined_col_index);
			uninlined_col_object_len = ValuePeeker::PeekObjectLength(uninlined_col_value);
			uninlined_col_object_ptr = static_cast<unsigned char *>(ValuePeeker::PeekObjectValue(uninlined_col_value));
			std::string uninlined_varchar_str(reinterpret_cast<char const*>(uninlined_col_object_ptr), uninlined_col_object_len);


			new_uninlined_col_value = new_tile->GetValue(tup_itr,uninlined_col_index);
			new_uninlined_col_object_len = ValuePeeker::PeekObjectLength(new_uninlined_col_value);
			new_uninlined_col_object_ptr = static_cast<unsigned char *>(ValuePeeker::PeekObjectValue(new_uninlined_col_value));
			std::string new_uninlined_varchar_str(reinterpret_cast<char const*>(new_uninlined_col_object_ptr), new_uninlined_col_object_len);


			/*
			 * Check whether old and new information are same
			 */
			int is_value_same = uninlined_col_value.Compare(new_uninlined_col_value);
			int is_length_same = uninlined_col_object_len == uninlined_col_object_len;
			int is_pointer_same = uninlined_col_object_ptr == new_uninlined_col_object_ptr;
			int is_data_same = std::strcmp(uninlined_varchar_str.c_str(), new_uninlined_varchar_str.c_str());

			if(is_value_same || !is_length_same || is_pointer_same || is_data_same) {
				intended_behavior = false;
			}
		}
	}

	EXPECT_EQ(true, intended_behavior);


	delete tuple1;
	delete tuple2;
	delete tuple3;
	delete tile;
	delete new_tile;
    delete tile_group;
    delete backend;
    delete new_backend;
    delete schema;
}

} // End test namespace
} // End peloton namespace






