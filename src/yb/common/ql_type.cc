// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/common/ql_type.h"

namespace yb {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------
// The following functions are to construct QLType objects.

shared_ptr<QLType> QLType::Create(DataType data_type, const vector<shared_ptr<QLType>>& params) {
  switch (data_type) {
    case DataType::LIST:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::LIST>(params);
    case DataType::MAP:
      DCHECK_EQ(params.size(), 2);
      return CreateCollectionType<DataType::MAP>(params);
    case DataType::SET:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::SET>(params);
    case DataType::FROZEN:
      DCHECK_EQ(params.size(), 1);
      return CreateCollectionType<DataType::FROZEN>(params);
    case DataType::TUPLE:
      return CreateCollectionType<DataType::TUPLE>(params);
    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return nullptr;
    default:
      DCHECK_EQ(params.size(), 0);
      return Create(data_type);
  }
}

shared_ptr<QLType> QLType::Create(DataType data_type) {
  switch (data_type) {
    case DataType::UNKNOWN_DATA:
      return CreatePrimitiveType<DataType::UNKNOWN_DATA>();
    case DataType::NULL_VALUE_TYPE:
      return CreatePrimitiveType<DataType::NULL_VALUE_TYPE>();
    case DataType::INT8:
      return CreatePrimitiveType<DataType::INT8>();
    case DataType::INT16:
      return CreatePrimitiveType<DataType::INT16>();
    case DataType::INT32:
      return CreatePrimitiveType<DataType::INT32>();
    case DataType::INT64:
      return CreatePrimitiveType<DataType::INT64>();
    case DataType::STRING:
      return CreatePrimitiveType<DataType::STRING>();
    case DataType::BOOL:
      return CreatePrimitiveType<DataType::BOOL>();
    case DataType::FLOAT:
      return CreatePrimitiveType<DataType::FLOAT>();
    case DataType::DOUBLE:
      return CreatePrimitiveType<DataType::DOUBLE>();
    case DataType::BINARY:
      return CreatePrimitiveType<DataType::BINARY>();
    case DataType::TIMESTAMP:
      return CreatePrimitiveType<DataType::TIMESTAMP>();
    case DataType::DECIMAL:
      return CreatePrimitiveType<DataType::DECIMAL>();
    case DataType::VARINT:
      return CreatePrimitiveType<DataType::VARINT>();
    case DataType::INET:
      return CreatePrimitiveType<DataType::INET>();
    case DataType::JSONB:
      return CreatePrimitiveType<DataType::JSONB>();
    case DataType::UUID:
      return CreatePrimitiveType<DataType::UUID>();
    case DataType::TIMEUUID:
      return CreatePrimitiveType<DataType::TIMEUUID>();
    case DataType::DATE:
      return CreatePrimitiveType<DataType::DATE>();
    case DataType::TIME:
      return CreatePrimitiveType<DataType::TIME>();

    // Create empty parametric types and raise error during semantic check.
    case DataType::LIST:
      return CreateTypeList();
    case DataType::MAP:
      return CreateTypeMap();
    case DataType::SET:
      return CreateTypeSet();
    case DataType::TUPLE:
      return CreateCollectionType<DataType::TUPLE>({});
    case DataType::FROZEN:
      return CreateTypeFrozen();

    // Kudu datatypes.
    case UINT8:
      return CreatePrimitiveType<DataType::UINT8>();
    case UINT16:
      return CreatePrimitiveType<DataType::UINT16>();
    case UINT32:
      return CreatePrimitiveType<DataType::UINT32>();
    case UINT64:
      return CreatePrimitiveType<DataType::UINT64>();

    // Datatype for variadic builtin function.
    case TYPEARGS:
      return CreatePrimitiveType<DataType::TYPEARGS>();

    // User-defined types cannot be created like this
    case DataType::USER_DEFINED_TYPE:
      LOG(FATAL) << "Unsupported constructor for user-defined type";
      return nullptr;

    default:
      LOG(FATAL) << "Not supported datatype " << ToCQLString(data_type);
      return nullptr;
  }
}

bool QLType::IsValidPrimaryType(DataType type) {
  switch (type) {
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::JSONB: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE:
      return false;

    default:
      // Let all other types go. Because we already process column datatype before getting here,
      // just assume that they are all valid types.
      return true;
  }
}

shared_ptr<QLType> QLType::CreateTypeMap(std::shared_ptr<QLType> key_type,
                                           std::shared_ptr<QLType> value_type) {
  vector<shared_ptr<QLType>> params = {key_type, value_type};
  return CreateCollectionType<DataType::MAP>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeMap(DataType key_type, DataType value_type) {
  return CreateTypeMap(QLType::Create(key_type), QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeList(std::shared_ptr<QLType> value_type) {
  vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::LIST>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeList(DataType value_type) {
  return CreateTypeList(QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeSet(std::shared_ptr<QLType> value_type) {
  vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::SET>(params);
}

std::shared_ptr<QLType>  QLType::CreateTypeSet(DataType value_type) {
  return CreateTypeSet(QLType::Create(value_type));
}

shared_ptr<QLType> QLType::CreateTypeFrozen(shared_ptr<QLType> value_type) {
  vector<shared_ptr<QLType>> params(1, value_type);
  return CreateCollectionType<DataType::FROZEN>(params);
}

//--------------------------------------------------------------------------------------------------
// ToPB and FromPB.

void QLType::ToQLTypePB(QLTypePB *pb_type) const {
  pb_type->set_main(id_);
  for (auto &param : params_) {
    param->ToQLTypePB(pb_type->add_params());
  }

  if (IsUserDefined()) {
    auto udtype_info = pb_type->mutable_udtype_info();
    udtype_info->set_keyspace_name(udtype_keyspace_name());
    udtype_info->set_name(udtype_name());
    udtype_info->set_id(udtype_id());

    for (const auto &field_name : udtype_field_names()) {
      udtype_info->add_field_names(field_name);
    }
  }
}

shared_ptr<QLType> QLType::FromQLTypePB(const QLTypePB& pb_type) {
  if (pb_type.main() == USER_DEFINED_TYPE) {
    auto ql_type = std::make_shared<QLType>(pb_type.udtype_info().keyspace_name(),
                                              pb_type.udtype_info().name());
    std::vector<string> field_names;
    for (const auto& field_name : pb_type.udtype_info().field_names()) {
      field_names.push_back(field_name);
    }

    std::vector<shared_ptr<QLType>> field_types;
    for (const auto& field_type : pb_type.params()) {
      field_types.push_back(QLType::FromQLTypePB(field_type));
    }

    ql_type->SetUDTypeFields(pb_type.udtype_info().id(), field_names, field_types);
    return ql_type;
  }

  if (pb_type.params().empty()) {
    return Create(pb_type.main());
  }

  vector<shared_ptr<QLType>> params;
  for (auto &param : pb_type.params()) {
    params.push_back(FromQLTypePB(param));
  }
  return Create(pb_type.main(), params);
}

//--------------------------------------------------------------------------------------------------
// Logging routines.
const string QLType::ToCQLString(const DataType& datatype) {
  switch (datatype) {
    case DataType::UNKNOWN_DATA: return "unknown";
    case DataType::NULL_VALUE_TYPE: return "anytype";
    case DataType::INT8: return "tinyint";
    case DataType::INT16: return "smallint";
    case DataType::INT32: return "int";
    case DataType::INT64: return "bigint";
    case DataType::STRING: return "text";
    case DataType::BOOL: return "boolean";
    case DataType::FLOAT: return "float";
    case DataType::DOUBLE: return "double";
    case DataType::BINARY: return "blob";
    case DataType::TIMESTAMP: return "timestamp";
    case DataType::DECIMAL: return "decimal";
    case DataType::VARINT: return "varint";
    case DataType::INET: return "inet";
    case DataType::JSONB: return "jsonb";
    case DataType::LIST: return "list";
    case DataType::MAP: return "map";
    case DataType::SET: return "set";
    case DataType::UUID: return "uuid";
    case DataType::TIMEUUID: return "timeuuid";
    case DataType::TUPLE: return "tuple";
    case DataType::TYPEARGS: return "typeargs";
    case DataType::FROZEN: return "frozen";
    case DataType::USER_DEFINED_TYPE: return "user_defined_type";
    case DataType::DATE: return "date";
    case DataType::TIME: return "time";
    case DataType::UINT8: return "uint8";
    case DataType::UINT16: return "uint16";
    case DataType::UINT32: return "uint32";
    case DataType::UINT64: return "uint64";
  }
  LOG (FATAL) << "Invalid datatype: " << datatype;
  return "Undefined Type";
}

const string QLType::ToString() const {
  std::stringstream ss;
  ToString(ss);
  return ss.str();
}

void QLType::ToString(std::stringstream& os) const {
  if (IsUserDefined()) {
    // UDTs can only be used in the keyspace they are defined in, so keyspace name is implied.
    os << udtype_name();
  } else {
    os << ToCQLString(id_);
    if (!params_.empty()) {
      os << "<";
      for (int i = 0; i < params_.size(); i++) {
        if (i > 0) {
          os << ", ";
        }
        params_[i]->ToString(os);
      }
      os << ">";
    }
  }
}

}  // namespace yb
