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

  bool EnterCollectionScope();
  void LeaveCollectionScope();

  void Iterate(ObjectVisitor* v);

  Handle<Object> RedirectLoad(Handle<Object> obj, bool* terminate);
  Handle<Object> RedirectStore(Handle<Object> obj, bool* terminate);

  void StartTransaction();
  bool CommitTransaction();

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(STM);

  void PauseForGC();

  volatile Atomic32 need_gc_;

  // commit_mutex_ must be acquired before transactions_mutex_
  // heap_mutex_ is independent from them
  Mutex* heap_mutex_;
  Mutex* commit_mutex_;
  Mutex* transactions_mutex_;

  List<Transaction*> transactions_;

  Isolate* isolate_;

  friend class Isolate;
};

} } // namespace v8::internal

#endif // V8_STM_H_
