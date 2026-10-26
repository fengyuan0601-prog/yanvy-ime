// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic TSF scheduling fixture. No system registration, window, input
// injection, broker or document text is used. The context models Microsoft's
// documented rule that synchronous requests precede queued asynchronous ones.
#include "edit_session.hpp"
#include <deque>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
class Context final : public ITfContext {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=IID_IUnknown&&iid!=IID_ITfContext)return E_NOINTERFACE;
    *out=static_cast<ITfContext*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
  ULONG STDMETHODCALLTYPE Release() override{auto refs=InterlockedDecrement(&refs_);if(!refs)delete this;return refs;}
  HRESULT STDMETHODCALLTYPE RequestEditSession(TfClientId,ITfEditSession* edit,DWORD flags,HRESULT* result) override {
    if(reject){*result=E_FAIL;return E_FAIL;}
    if(flags&TF_ES_SYNC){
      ++sync_requests;
      if(block_next_sync){block_next_sync=false;*result=TF_E_SYNCHRONOUS;return S_OK;}
      *result=edit->DoEditSession(1);return S_OK;
    }
    if(flags&TF_ES_ASYNC){++async_requests;queue_.emplace_back(edit);*result=TF_S_ASYNC;return S_OK;}
    return E_INVALIDARG;
  }
  void DrainOne(){if(!queue_.empty()){auto edit=queue_.front();queue_.pop_front();edit->DoEditSession(1);}}
  void Drain(){while(!queue_.empty())DrainOne();}
  void Discard(){queue_.clear();}
  HRESULT STDMETHODCALLTYPE InWriteSession(TfClientId,BOOL*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetSelection(TfEditCookie,ULONG,ULONG,TF_SELECTION*,ULONG*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE SetSelection(TfEditCookie,ULONG,const TF_SELECTION*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetStart(TfEditCookie,ITfRange**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetEnd(TfEditCookie,ITfRange**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetActiveView(ITfContextView**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE EnumViews(IEnumTfContextViews**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetStatus(TF_STATUS*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetProperty(REFGUID,ITfProperty**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetAppProperty(REFGUID,ITfReadOnlyProperty**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE TrackProperties(const GUID**,ULONG,const GUID**,ULONG,ITfReadOnlyProperty**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE EnumProperties(IEnumTfProperties**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE CreateRangeBackup(TfEditCookie,ITfRange*,ITfRangeBackup**) override{return E_NOTIMPL;}
  bool block_next_sync=true,reject=false;
  int sync_requests=0,async_requests=0;
 private:
  LONG refs_=1;
  std::deque<ComPtr<ITfEditSession>> queue_;
};
struct Harness {
  EditSessionOrder order;
  ComPtr<Context> context;
  Harness(){context.Attach(new Context);}
  ~Harness(){context->Discard();}
  HRESULT Schedule(std::function<HRESULT(TfEditCookie)> function,bool write=true){
    return RequestOrderedEditSession(context.Get(),context.Get(),1,order,std::move(function),write);
  }
};
class LifetimeOwner final : public IUnknown {
 public:
  LifetimeOwner(bool& destroyed,bool& drained):destroyed_(destroyed),drained_(drained){}
  ~LifetimeOwner(){destroyed_=true;drained_=order.pending==0;}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=IID_IUnknown)return E_NOINTERFACE;
    *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
  ULONG STDMETHODCALLTYPE Release() override{auto refs=InterlockedDecrement(&refs_);if(!refs)delete this;return refs;}
  EditSessionOrder order;
  int generation=0;
 private:
  LONG refs_=1;
  bool& destroyed_;
  bool& drained_;
};
int checks=0;
void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;std::cout<<"PASS "<<name<<'\n';}
}
int wmain(){
  try{
    {
      Harness h;std::wstring preedit,committed;
      h.Schedule([&](TfEditCookie){preedit=L"n";return S_OK;});
      h.Schedule([&](TfEditCookie){preedit=L"ni";return S_OK;});
      h.Schedule([&](TfEditCookie){preedit.clear();committed+=L"你好";return S_OK;});
      Check(h.context->sync_requests==1&&h.order.pending==3,"only the first edit attempts synchronous execution");
      h.context->Drain();
      Check(preedit.empty()&&committed==L"你好","queued preedit cannot overwrite a later commit");
      Check(h.order.pending==0,"completed queue releases scheduling state");
      auto result=h.Schedule([](TfEditCookie){return S_OK;});
      Check(result==S_OK&&h.context->sync_requests==2,"synchronous fast path resumes after queue drains");
    }
    {
      Harness h;std::wstring committed;
      h.Schedule([&](TfEditCookie){committed+=L"你";return S_OK;});
      h.Schedule([&](TfEditCookie){committed+=L"好";return S_OK;});
      h.context->Drain();
      Check(committed==L"你好","every queued commit is retained in original order");
    }
    {
      // A new key can arrive between asynchronous callbacks. The scheduler
      // must preserve document edits already queued, including commit frames.
      Harness h;std::vector<int> events;
      h.Schedule([&](TfEditCookie){events.push_back(1);return S_OK;});
      h.Schedule([&](TfEditCookie){events.push_back(2);return S_OK;});
      h.context->DrainOne();
      h.Schedule([&](TfEditCookie){events.push_back(3);return S_OK;});
      Check(events==std::vector<int>({1})&&h.order.pending==2,"a key arriving between callbacks cannot overtake queued edits");
      h.context->Drain();
      Check(events==std::vector<int>({1,2,3}),"interleaved key and commit frames retain their order");
    }
    {
      Harness h;std::wstring text=L"old";std::vector<int> events;
      h.Schedule([&](TfEditCookie){text.clear();events.push_back(1);return S_OK;});
      h.Schedule([&](TfEditCookie){text=L"new";events.push_back(2);return S_OK;});
      h.Schedule([&](TfEditCookie){events.push_back(3);return S_OK;},false);
      h.context->Drain();
      Check(text==L"new"&&events==std::vector<int>({1,2,3}),"cleanup replacement and read-only positioning remain ordered");
    }
    {
      Harness h;h.context->block_next_sync=false;int calls=0;
      auto result=h.Schedule([&](TfEditCookie){++calls;return TS_E_NOLOCK;});
      h.context->Drain();
      Check(result==TS_E_NOLOCK&&calls==1&&h.order.pending==0,"a callback that ran and failed is never retried");
    }
    {
      Harness h;h.context->reject=true;int calls=0;
      auto result=h.Schedule([&](TfEditCookie){++calls;return S_OK;});
      Check(FAILED(result)&&calls==0&&h.order.pending==0,"rejected requests leave no pending edit");
    }
    {
      Harness h;int calls=0;
      h.Schedule([&](TfEditCookie){++calls;return S_OK;});
      h.context->Discard();
      Check(calls==0&&h.order.pending==0,"discarded asynchronous requests release scheduling state");
    }
    {
      Harness h;std::vector<int> events;
      h.Schedule([&](TfEditCookie){events.push_back(1);h.Schedule([&](TfEditCookie){events.push_back(3);return S_OK;});return S_OK;});
      h.Schedule([&](TfEditCookie){events.push_back(2);return S_OK;});
      h.context->Drain();
      Check(events==std::vector<int>({1,2,3})&&h.order.pending==0,"reentrant edits queue after already pending edits");
    }
    {
      Harness h;h.context->block_next_sync=false;
      auto result=h.Schedule([](TfEditCookie)->HRESULT{throw std::runtime_error("synthetic");});
      Check(result==E_FAIL&&h.order.pending==0,"callback exceptions release scheduling state");
    }
    {
      Harness h;std::vector<int> events;h.context->block_next_sync=false;
      h.Schedule([&](TfEditCookie){events.push_back(1);h.Schedule([&](TfEditCookie){events.push_back(3);return S_OK;});events.push_back(2);return S_OK;});
      Check(events==std::vector<int>({1,2})&&h.order.pending==1,"a synchronous callback cannot reenter an edit");
      h.context->Drain();
      Check(events==std::vector<int>({1,2,3})&&h.order.pending==0,"nested queued edit runs after the synchronous callback completes");
    }
    {
      Harness h;
      h.Schedule([](TfEditCookie)->HRESULT{throw std::runtime_error("synthetic");});
      h.context->Drain();
      Check(h.order.pending==0,"asynchronous callback exceptions release scheduling state");
    }
    {
      ComPtr<Context> context;context.Attach(new Context);
      bool destroyed=false,drained=false,applied=false;
      auto owner=new LifetimeOwner(destroyed,drained);
      const int generation=owner->generation;
      RequestOrderedEditSession(owner,context.Get(),1,owner->order,[&,owner,generation](TfEditCookie){
        if(generation==owner->generation)applied=true;
        return S_OK;
      });
      ++owner->generation; // Service::Deactivate/Cancel invalidate queued edits.
      owner->Release();
      Check(!destroyed,"queued callback retains its service during deactivation");
      context->Drain();
      Check(destroyed&&drained&&!applied,"invalidated callback does not write and releases the service after completion");
    }
    std::cout<<checks<<" ordered edit session checks PASS\n";
    return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
