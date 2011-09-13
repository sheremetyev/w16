#include "v8.h"
#include "stm.h"
#include "isolate.h"

namespace v8 {
namespace internal {

class Transaction {
};

STM::STM() : heap_mutex_(OS::CreateMutex()) {
}

void STM::EnterAllocationScope() {
  heap_mutex_->Lock();
}

void STM::LeaveAllocationScope() {
  heap_mutex_->Unlock();
}

void STM::EnterCollectionScope() {
  heap_mutex_->Lock();
}

void STM::LeaveCollectionScope() {
  heap_mutex_->Unlock();
}

Handle<Object> STM::RedirectLoad(const Handle<Object>& obj) {
  return obj;
}

Handle<Object> STM::RedirectStore(const Handle<Object>& obj) {
  return obj;
}

Transaction* STM::StartTransaction() {
  return NULL;
}

bool STM::CommitTransaction(Transaction*) {
  return true;
}

} } // namespace v8::internal
