#ifndef V8_STM_H_
#define V8_STM_H_

#include "globals.h"
#include "list-inl.h"

namespace v8 {
namespace internal {

// suppose that each thread is scheduled on individual core
class CoreId {
public:
  static CoreId Current();
  static int CurrentInt() { return Current().ToInteger(); }

  int ToInteger() const { return id_; }

  static int const kMaxCores = 8;

private:
  CoreId(int id) : id_(id) { }

  int id_;
};

class Transaction;

class STM {
public:
  Transaction* StartTransaction();
  bool CommitTransaction(Transaction* trans);

  JSObject* RedirectRead(JSObject* obj);
  JSObject* RedirectWrite(JSObject* obj);

private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(STM);

  Isolate* isolate_;

  Mutex* transactions_mutex_;
  List<Transaction*> transactions_;

  Mutex* commit_mutex_;

  friend class Isolate;
};

} }  // namespace v8::internal

#endif  // V8_STM_H_
