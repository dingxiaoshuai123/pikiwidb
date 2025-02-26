/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */
#include "cmd_hash.h"

#include <config.h>

#include "pstd/pstd_string.h"
#include "store.h"

namespace pikiwidb {

HSetCmd::HSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HSetCmd::DoInitial(PClient* client) {
  if (client->argv_.size() % 2 != 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameHSet);
    return false;
  }
  client->SetKey(client->argv_[1]);
  client->ClearFvs();
  return true;
}

void HSetCmd::DoCmd(PClient* client) {
  int32_t ret = 0;
  storage::Status s;
  auto fvs = client->Fvs();

  for (size_t i = 2; i < client->argv_.size(); i += 2) {
    auto field = client->argv_[i];
    auto value = client->argv_[i + 1];
    int32_t temp = 0;
    // TODO(century): current bw doesn't support multiple fvs, fix it when necessary
    s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HSet(client->Key(), field, value, &temp);
    if (s.ok()) {
      ret += temp;
    } else if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      // FIXME(century): need txn, if bw crashes, it should rollback
      client->SetRes(CmdRes::kErrOther);
      return;
    }
  }

  client->AppendInteger(ret);
}

HGetCmd::HGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HGetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HGetCmd::DoCmd(PClient* client) {
  PString value;
  auto field = client->argv_[2];
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HGet(client->Key(), field, &value);
  if (s.ok()) {
    client->AppendString(value);
  } else if (s.IsNotFound()) {
    client->AppendString("");
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "hget cmd error");
  }
}

HDelCmd::HDelCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HDelCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HDelCmd::DoCmd(PClient* client) {
  int32_t res{};
  std::vector<std::string> fields(client->argv_.begin() + 2, client->argv_.end());
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HDel(client->Key(), fields, &res);
  if (!s.ok() && !s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
    return;
  }
  client->AppendInteger(res);
}

HMSetCmd::HMSetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HMSetCmd::DoInitial(PClient* client) {
  if (client->argv_.size() % 2 != 0) {
    client->SetRes(CmdRes::kWrongNum, kCmdNameHMSet);
    return false;
  }
  client->SetKey(client->argv_[1]);
  client->ClearFvs();
  // set fvs
  for (size_t index = 2; index < client->argv_.size(); index += 2) {
    client->Fvs().push_back({client->argv_[index], client->argv_[index + 1]});
  }
  return true;
}

void HMSetCmd::DoCmd(PClient* client) {
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HMSet(client->Key(), client->Fvs());
  if (s.ok()) {
    client->SetRes(CmdRes::kOK);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

HMGetCmd::HMGetCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HMGetCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  client->ClearFields();
  for (size_t i = 2; i < client->argv_.size(); ++i) {
    client->Fields().push_back(client->argv_[i]);
  }
  return true;
}

void HMGetCmd::DoCmd(PClient* client) {
  std::vector<storage::ValueStatus> vss;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HMGet(client->Key(), client->Fields(), &vss);
  if (s.ok() || s.IsNotFound()) {
    client->AppendArrayLenUint64(vss.size());
    for (size_t i = 0; i < vss.size(); ++i) {
      if (vss[i].status.ok()) {
        client->AppendString(vss[i].value);
      } else {
        client->AppendString("");
      }
    }
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

HGetAllCmd::HGetAllCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HGetAllCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HGetAllCmd::DoCmd(PClient* client) {
  int64_t total_fv = 0;
  int64_t cursor = 0;
  int64_t next_cursor = 0;
  size_t raw_limit = g_config.max_client_response_size.load();
  std::string raw;
  std::vector<storage::FieldValue> fvs;
  storage::Status s;

  do {
    fvs.clear();
    s = PSTORE.GetBackend(client->GetCurrentDB())
            ->GetStorage()
            ->HScan(client->Key(), cursor, "*", PIKIWIDB_SCAN_STEP_LENGTH, &fvs, &next_cursor);
    if (!s.ok()) {
      raw.clear();
      total_fv = 0;
      break;
    } else {
      for (const auto& fv : fvs) {
        client->RedisAppendLenUint64(raw, fv.field.size(), "$");
        client->RedisAppendContent(raw, fv.field);
        client->RedisAppendLenUint64(raw, fv.value.size(), "$");
        client->RedisAppendContent(raw, fv.value);
      }
      if (raw.size() >= raw_limit) {
        client->SetRes(CmdRes::kErrOther, "Response exceeds the max-client-response-size limit");
        return;
      }
      total_fv += static_cast<int64_t>(fvs.size());
      cursor = next_cursor;
    }
  } while (cursor != 0);

  if (s.ok() || s.IsNotFound()) {
    client->AppendArrayLen(total_fv * 2);
    client->AppendStringRaw(raw);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

HKeysCmd::HKeysCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HKeysCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HKeysCmd::DoCmd(PClient* client) {
  std::vector<std::string> fields;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HKeys(client->Key(), &fields);
  if (s.ok() || s.IsNotFound()) {
    client->AppendArrayLenUint64(fields.size());
    for (const auto& field : fields) {
      client->AppendStringLenUint64(field.size());
      client->AppendContent(field);
    }
    // update fields
    client->Fields() = std::move(fields);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, s.ToString());
  }
}

HLenCmd::HLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HLenCmd::DoCmd(PClient* client) {
  int32_t len = 0;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HLen(client->Key(), &len);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(len);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hlen");
  }
}

HStrLenCmd::HStrLenCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HStrLenCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HStrLenCmd::DoCmd(PClient* client) {
  int32_t len = 0;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HStrlen(client->Key(), client->argv_[2], &len);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(len);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "something wrong in hstrlen");
  }
}

HScanCmd::HScanCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HScanCmd::DoInitial(PClient* client) {
  if (auto size = client->argv_.size(); size != 3 && size != 5 && size != 7) {
    client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
    return false;
  }
  client->SetKey(client->argv_[1]);
  return true;
}

void HScanCmd::DoCmd(PClient* client) {
  const auto& argv = client->argv_;
  // parse arguments
  int64_t cursor{};
  int64_t count{10};
  std::string pattern{"*"};
  if (pstd::String2int(argv[2], &cursor) == 0) {
    client->SetRes(CmdRes::kInvalidCursor, kCmdNameHScan);
    return;
  }
  for (size_t i = 3; i < argv.size(); i += 2) {
    if (auto lower = pstd::StringToLower(argv[i]); kMatchSymbol == lower) {
      pattern = argv[i + 1];
    } else if (kCountSymbol == lower) {
      if (pstd::String2int(argv[i + 1], &count) == 0) {
        client->SetRes(CmdRes::kInvalidInt, kCmdNameHScan);
        return;
      }
      if (count < 0) {
        client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
        return;
      }
    } else {
      client->SetRes(CmdRes::kSyntaxErr, kCmdNameHScan);
      return;
    }
  }

  // execute command
  std::vector<storage::FieldValue> fvs;
  int64_t next_cursor{};
  auto status = PSTORE.GetBackend(client->GetCurrentDB())
                    ->GetStorage()
                    ->HScan(client->Key(), cursor, pattern, count, &fvs, &next_cursor);
  if (!status.ok() && !status.IsNotFound()) {
    if (status.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, status.ToString());
    }
    return;
  }

  // reply to client
  client->AppendArrayLen(2);
  client->AppendString(std::to_string(next_cursor));
  client->AppendArrayLenUint64(fvs.size() * 2);
  for (const auto& [field, value] : fvs) {
    client->AppendString(field);
    client->AppendString(value);
  }
}

HValsCmd::HValsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HValsCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HValsCmd::DoCmd(PClient* client) {
  std::vector<std::string> valueVec;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HVals(client->Key(), &valueVec);
  if (s.ok() || s.IsNotFound()) {
    client->AppendStringVector(valueVec);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kErrOther, "hvals cmd error");
  }
}

HIncrbyFloatCmd::HIncrbyFloatCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HIncrbyFloatCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  long double long_double_by = 0;
  if (-1 == StrToLongDouble(client->argv_[3].c_str(), static_cast<int>(client->argv_[3].size()), &long_double_by)) {
    client->SetRes(CmdRes::kInvalidParameter);
    return false;
  }
  return true;
}

void HIncrbyFloatCmd::DoCmd(PClient* client) {
  long double long_double_by = 0;
  if (-1 == StrToLongDouble(client->argv_[3].c_str(), static_cast<int>(client->argv_[3].size()), &long_double_by)) {
    client->SetRes(CmdRes::kInvalidFloat);
    return;
  }
  std::string newValue;
  storage::Status s = PSTORE.GetBackend(client->GetCurrentDB())
                          ->GetStorage()
                          ->HIncrbyfloat(client->Key(), client->argv_[2], client->argv_[3], &newValue);
  if (s.ok() || s.IsNotFound()) {
    client->AppendString(newValue);
  } else if (s.IsInvalidArgument() &&
             s.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else if (s.IsCorruption() && s.ToString() == "Corruption: value is not a valid float") {
    client->SetRes(CmdRes::kInvalidFloat);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, "hvals cmd error");
  }
}

HSetNXCmd::HSetNXCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HSetNXCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HSetNXCmd::DoCmd(PClient* client) {
  int32_t temp = 0;
  storage::Status s;
  s = PSTORE.GetBackend(client->GetCurrentDB())
          ->GetStorage()
          ->HSetnx(client->Key(), client->argv_[2], client->argv_[3], &temp);
  if (s.ok()) {
    client->AppendInteger(temp);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kMultiKey);
  } else {
    client->SetRes(CmdRes::kSyntaxErr, "hsetnx cmd error");
  }
  return;
}

HIncrbyCmd::HIncrbyCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsWrite, kAclCategoryWrite | kAclCategoryHash) {}

bool HIncrbyCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HIncrbyCmd::DoCmd(PClient* client) {
  int64_t int_by = 0;
  if (!pstd::String2int(client->argv_[3].data(), client->argv_[3].size(), &int_by)) {
    client->SetRes(CmdRes::kInvalidParameter);
    return;
  }

  int64_t temp = 0;
  storage::Status s =
      PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HIncrby(client->Key(), client->argv_[2], int_by, &temp);
  if (s.ok() || s.IsNotFound()) {
    client->AppendInteger(temp);
  } else if (s.IsInvalidArgument() &&
             s.ToString().substr(0, std::char_traits<char>::length(ErrTypeMessage)) == ErrTypeMessage) {
    client->SetRes(CmdRes::kMultiKey);
  } else if (s.IsCorruption() && s.ToString() == "Corruption: hash value is not an integer") {
    client->SetRes(CmdRes::kInvalidInt);
  } else if (s.IsInvalidArgument()) {
    client->SetRes(CmdRes::kOverFlow);
  } else {
    client->SetRes(CmdRes::kErrOther, "hincrby cmd error");
  }
}

HRandFieldCmd::HRandFieldCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HRandFieldCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HRandFieldCmd::DoCmd(PClient* client) {
  // parse arguments
  const auto& argv = client->argv_;
  int64_t count{1};
  bool with_values{false};
  if (argv.size() > 2) {
    // redis checks the integer argument first and then the number of parameters
    if (pstd::String2int(argv[2], &count) == 0) {
      client->SetRes(CmdRes::kInvalidInt);
      return;
    }
    if (argv.size() > 4) {
      client->SetRes(CmdRes::kSyntaxErr);
      return;
    }
    if (argv.size() > 3) {
      if (kWithValueString != pstd::StringToLower(argv[3])) {
        client->SetRes(CmdRes::kSyntaxErr);
        return;
      }
      with_values = true;
    }
  }

  // execute command
  std::vector<std::string> res;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HRandField(client->Key(), count, with_values, &res);
  if (s.IsNotFound()) {
    client->AppendString("");
    return;
  }
  if (!s.ok()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
    return;
  }

  // reply to client
  if (argv.size() > 2) {
    client->AppendArrayLenUint64(res.size());
  }
  for (const auto& item : res) {
    client->AppendString(item);
  }
}

HExistsCmd::HExistsCmd(const std::string& name, int16_t arity)
    : BaseCmd(name, arity, kCmdFlagsReadonly, kAclCategoryRead | kAclCategoryHash) {}

bool HExistsCmd::DoInitial(PClient* client) {
  client->SetKey(client->argv_[1]);
  return true;
}

void HExistsCmd::DoCmd(PClient* client) {
  // parse arguments
  auto& field = client->argv_[2];

  // execute command
  std::vector<std::string> res;
  auto s = PSTORE.GetBackend(client->GetCurrentDB())->GetStorage()->HExists(client->Key(), field);
  if (!s.ok() && !s.IsNotFound()) {
    if (s.IsInvalidArgument()) {
      client->SetRes(CmdRes::kMultiKey);
    } else {
      client->SetRes(CmdRes::kErrOther, s.ToString());
    }
    return;
  }

  // reply
  client->AppendInteger(s.IsNotFound() ? 0 : 1);
}

}  // namespace pikiwidb
