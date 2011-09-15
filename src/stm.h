#ifndef V8_STM_H_
#define V8_STM_H_

#include "globals.h"

namespace v8 {
namespace internal {

class Transaction;

class STM {
 public:
  void EnterAllocationScope();
  void LeaveAllocationScope();

  void EnterCollectionScope();
  void LeaveCollectionScope();

  Handle<Object> RedirectLoad(Handle<Object> obj);
  Handle<Object> RedirectStore(Handle<Object> obj);

  void StartTransaction();
  bool CommitTransaction();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(STM);

  // TODO: specify order of lock acquisition
  Mutex* heap_mutex_;
  Mutex* commit_mutex_;
  Mutex* transactions_mutex_;

  List<Transaction*> transactions_;

  Isolate* isolate_;

  friend class Isolate;
};

} } // namespace v8::internal

#endif // V8_STM_H_
