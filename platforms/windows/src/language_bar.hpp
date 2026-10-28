// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <olectl.h>
#include <wrl/client.h>
#include <atomic>
#include <functional>
#include <utility>

// Public SDK declarations missing from MinGW-w64's ctfutb.h. ABI verified
// against microsoft/win32metadata's RecompiledIdlHeaders/um/ctfutb.h.
#ifndef __ITfLangBarItemButton_INTERFACE_DEFINED__
#define __ITfLangBarItemButton_INTERFACE_DEFINED__
struct ITfMenu;
enum TfLBIClick { TF_LBI_CLK_RIGHT=1, TF_LBI_CLK_LEFT=2 };
inline constexpr IID IID_ITfLangBarItemButton{0x28c7f1d0,0xde25,0x11d2,{0xaf,0xdd,0x00,0x10,0x5a,0x27,0x99,0xb5}};
struct ITfLangBarItemButton : ITfLangBarItem {
  virtual HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick,POINT,const RECT*)=0;
  virtual HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*)=0;
  virtual HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT)=0;
  virtual HRESULT STDMETHODCALLTYPE GetIcon(HICON*)=0;
  virtual HRESULT STDMETHODCALLTYPE GetText(BSTR*)=0;
};
#endif
#ifndef TF_LBI_ICON
#define TF_LBI_ICON 0x00000001
#define TF_LBI_TEXT 0x00000002
#define TF_LBI_TOOLTIP 0x00000004
#define TF_LBI_STATUS 0x00010000
#define TF_LBI_STYLE_HIDDENSTATUSCONTROL 0x00000001
#define TF_LBI_STYLE_TEXTCOLORICON 0x00000020
#define TF_LBI_STYLE_BTN_BUTTON 0x00010000
#define TF_LBI_STATUS_HIDDEN 0x00000001
#endif

namespace yanvy {
// GUID_LBI_INPUTMODE, the one mode item accepted by the Windows Input Indicator.
inline constexpr GUID kNativeInputModeItem{0x2c77a81e,0x41cc,0x4178,{0xa3,0xa7,0x5f,0x8a,0x98,0x75,0x68,0xe6}};

inline HICON CreateModeIcon(bool ascii,int width,int height) {
  if(width<=0||height<=0||width>256||height>256)return nullptr;
  HDC dc=CreateCompatibleDC(nullptr);
  HBITMAP mask=CreateBitmap(width,height*2,1,1,nullptr);
  HFONT font=CreateFontW(-height,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
  HICON icon=nullptr;
  if(dc&&mask&&font){
    const auto old_bitmap=SelectObject(dc,mask),old_font=SelectObject(dc,font);
    // Monochrome icon: white AND/black XOR is transparent; black AND/XOR is
    // foreground text. TEXTCOLORICON lets Windows recolor it for either theme.
    PatBlt(dc,0,0,width,height,WHITENESS);
    PatBlt(dc,0,height,width,height,BLACKNESS);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));
    RECT bounds{0,0,width,height};
    DrawTextW(dc,ascii?L"英":L"中",1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old_font);SelectObject(dc,old_bitmap);
    ICONINFO info{};info.fIcon=TRUE;info.hbmMask=mask;
    icon=CreateIconIndirect(&info);
  }
  if(font)DeleteObject(font);
  if(mask)DeleteObject(mask);
  if(dc)DeleteDC(dc);
  return icon;
}

class ModeLanguageBar final : public ITfLangBarItemButton,public ITfSource {
 public:
  ModeLanguageBar(REFCLSID service,bool ascii,std::function<void()> toggle,
      std::function<void()> settings,std::atomic<long>* objects=nullptr)
      :module_pin_(objects),ascii_(ascii),toggle_(std::move(toggle)),settings_(std::move(settings)){
    info_.clsidService=service;info_.guidItem=kNativeInputModeItem;
    info_.dwStyle=TF_LBI_STYLE_BTN_BUTTON|TF_LBI_STYLE_TEXTCOLORICON|TF_LBI_STYLE_HIDDENSTATUSCONTROL;
    lstrcpynW(info_.szDescription,L"言舟中英文切换",TF_LBI_DESC_MAXLEN);
  }
  ~ModeLanguageBar()=default;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid==IID_IUnknown||iid==IID_ITfLangBarItem||iid==IID_ITfLangBarItemButton)
      *out=static_cast<ITfLangBarItemButton*>(this);
    else if(iid==IID_ITfSource)*out=static_cast<ITfSource*>(this);
    else return E_NOINTERFACE;
    AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&refs_);}
  ULONG STDMETHODCALLTYPE Release() override {const auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO* out) override {if(!out)return E_POINTER;*out=info_;return S_OK;}
  HRESULT STDMETHODCALLTYPE GetStatus(DWORD* out) override {if(!out)return E_POINTER;*out=hidden_?TF_LBI_STATUS_HIDDEN:0;return S_OK;}
  HRESULT STDMETHODCALLTYPE Show(BOOL show) override {
    if(hidden_==!show)return S_OK;
    hidden_=!show;return Notify(TF_LBI_STATUS);
  }
  HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR* out) override {
    return Text(ascii_?L"言舟：英文 · 左键切换中英文，右键打开设置（Ctrl+空格）":
        L"言舟：中文 · 左键切换中英文，右键打开设置（Ctrl+空格）",out);
  }
  HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click,POINT,const RECT*) override {
    // No popup is positioned here: right-click opens the existing settings
    // window directly, without changing the host's DPI awareness or focus UI.
    Microsoft::WRL::ComPtr<ModeLanguageBar> keep_alive=this;
    if(disconnected_)return S_FALSE;
    try {
      auto action=click==TF_LBI_CLK_LEFT?toggle_:click==TF_LBI_CLK_RIGHT?settings_:std::function<void()>{};
      if(!action)return S_FALSE;
      action();return S_OK;
    }catch(...){return E_FAIL;}
  }
  HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*) override {return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) override {return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetIcon(HICON* out) override {
    if(!out)return E_POINTER;
    // TSF owns each returned icon and calls DestroyIcon. Metrics follow the
    // caller's DPI context, per the GetIcon contract; no process DPI changes.
    *out=CreateModeIcon(ascii_,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));
    return *out?S_OK:E_FAIL;
  }
  HRESULT STDMETHODCALLTYPE GetText(BSTR* out) override {return Text(ascii_?L"英":L"中",out);}
  HRESULT STDMETHODCALLTYPE AdviseSink(REFIID iid,IUnknown* unknown,DWORD* cookie) override {
    if(!cookie)return E_POINTER;
    *cookie=TF_INVALID_COOKIE;
    if(iid!=IID_ITfLangBarItemSink)return CONNECT_E_CANNOTCONNECT;
    if(!unknown)return E_INVALIDARG;
    if(disconnected_)return CONNECT_E_CANNOTCONNECT;
    if(sink_advised_)return CONNECT_E_ADVISELIMIT;
    HRESULT hr=unknown->QueryInterface(IID_ITfLangBarItemSink,reinterpret_cast<void**>(sink_.GetAddressOf()));
    if(SUCCEEDED(hr)){sink_advised_=true;*cookie=kSinkCookie;}
    return hr;
  }
  HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
    if(cookie!=kSinkCookie||!sink_advised_)return CONNECT_E_NOCONNECTION;
    sink_advised_=false;
    sink_.Reset();return S_OK;
  }
  void SetAscii(bool ascii){if(ascii_!=ascii){ascii_=ascii;Notify(TF_LBI_ICON|TF_LBI_TEXT|TF_LBI_TOOLTIP);}}
  void Disconnect(){
    disconnected_=true;toggle_={};settings_={};sink_.Reset();
    // RemoveItem may still unadvise after disconnecting. Keep its cookie valid
    // for that final call without retaining a sink/item reference cycle.
  }
 private:
  struct ModulePin {
    explicit ModulePin(std::atomic<long>* count):count_(count){if(count_)++*count_;}
    ~ModulePin(){if(count_)--*count_;}
    std::atomic<long>* count_;
  };
  static HRESULT Text(const wchar_t* text,BSTR* out){if(!out)return E_POINTER;*out=SysAllocString(text);return *out?S_OK:E_OUTOFMEMORY;}
  HRESULT Notify(DWORD flags){Microsoft::WRL::ComPtr<ModeLanguageBar> keep_alive=this;auto sink=sink_;return sink?sink->OnUpdate(flags):S_OK;}
  static constexpr DWORD kSinkCookie=1;
  // Declared first so callbacks and the COM sink release while the DLL is
  // still pinned; a sink Release can re-enter DllCanUnloadNow.
  ModulePin module_pin_;
  LONG refs_=1;
  TF_LANGBARITEMINFO info_{};
  bool ascii_=false,hidden_=false,disconnected_=false,sink_advised_=false;
  std::function<void()> toggle_,settings_;
  Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
};

class LanguageBarBinding {
 public:
  LanguageBarBinding()=default;
  LanguageBarBinding(const LanguageBarBinding&)=delete;
  LanguageBarBinding& operator=(const LanguageBarBinding&)=delete;
  ~LanguageBarBinding(){Detach();}
  HRESULT Attach(ITfLangBarItemMgr* manager,ModeLanguageBar* item){
    if(manager_||item_)return E_UNEXPECTED;
    if(!manager||!item)return E_INVALIDARG;
    manager_=manager;item_=item;
    const HRESULT hr=manager_->AddItem(item_.Get());
    if(FAILED(hr)){item_->Disconnect();item_.Reset();manager_.Reset();}
    return hr;
  }
  void Detach(){
    auto item=std::move(item_);auto manager=std::move(manager_);
    if(item){item->Disconnect();if(manager)manager->RemoveItem(item.Get());}
  }
 private:
  Microsoft::WRL::ComPtr<ITfLangBarItemMgr> manager_;
  Microsoft::WRL::ComPtr<ModeLanguageBar> item_;
};
}
