// SPDX-License-Identifier: BSD-3-Clause
// Pure COM/GDI fixture. No live TSF AddItem, windows, input or settings launch.
#include "language_bar.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
std::wstring ReadText(ITfLangBarItemButton* item){BSTR value=nullptr;Check(SUCCEEDED(item->GetText(&value))&&value,"GetText");std::wstring text(value,SysStringLen(value));SysFreeString(value);return text;}
std::wstring ReadTooltip(ITfLangBarItemButton* item){BSTR value=nullptr;Check(SUCCEEDED(item->GetTooltipString(&value))&&value,"GetTooltipString");std::wstring text(value,SysStringLen(value));SysFreeString(value);return text;}
struct Sink final:ITfLangBarItemSink {
  ULONG refs=1;unsigned updates=0;DWORD flags=0;std::function<void()> during_update;
  ComPtr<ITfLangBarItem> held;std::function<void()> during_destroy;
  ~Sink(){if(during_destroy)during_destroy();}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=IID_IUnknown&&iid!=IID_ITfLangBarItemSink)return E_NOINTERFACE;
    *out=static_cast<ITfLangBarItemSink*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE OnUpdate(DWORD f) override{++updates;flags=f;if(during_update)during_update();return S_OK;}
};
struct Manager final:ITfLangBarItemMgr {
  ULONG refs=1;unsigned adds=0,removes=0;HRESULT add_result=S_OK,remove_result=S_OK;
  DWORD sink_cookie=TF_INVALID_COOKIE;HRESULT unadvise_result=E_UNEXPECTED;
  ComPtr<ITfLangBarItem> held;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=IID_IUnknown&&iid!=IID_ITfLangBarItemMgr)return E_NOINTERFACE;
    *out=static_cast<ITfLangBarItemMgr*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE AddItem(ITfLangBarItem* item) override{++adds;if(SUCCEEDED(add_result))held=item;return add_result;}
  HRESULT STDMETHODCALLTYPE RemoveItem(ITfLangBarItem* item) override{
    ++removes;
    if(sink_cookie!=TF_INVALID_COOKIE){
      ComPtr<ITfSource> source;
      if(SUCCEEDED(item->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.GetAddressOf()))))
        unadvise_result=source->UnadviseSink(sink_cookie);
    }
    if(SUCCEEDED(remove_result)&&held.Get()==item)held.Reset();
    return remove_result;
  }
  HRESULT STDMETHODCALLTYPE EnumItems(IEnumTfLangBarItems**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetItem(REFGUID,ITfLangBarItem**) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE AdviseItemSink(ITfLangBarItemSink*,DWORD*,REFGUID) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE UnadviseItemSink(DWORD) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetItemFloatingRect(DWORD,REFGUID,RECT*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetItemsStatus(ULONG,const GUID*,DWORD*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetItemNum(ULONG*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE GetItems(ULONG,ITfLangBarItem**,TF_LANGBARITEMINFO*,DWORD*,ULONG*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE AdviseItemsSink(ULONG,ITfLangBarItemSink**,const GUID*,DWORD*) override{return E_NOTIMPL;}
  HRESULT STDMETHODCALLTYPE UnadviseItemsSink(ULONG,DWORD*) override{return E_NOTIMPL;}
};
std::vector<unsigned char> IconMask(HICON icon,int size){
  ICONINFO info{};Check(GetIconInfo(icon,&info)!=FALSE,"GetIconInfo");
  BITMAP bitmap{};Check(!info.hbmColor&&GetObjectW(info.hbmMask,sizeof(bitmap),&bitmap)==sizeof(bitmap)&&
      bitmap.bmWidth==size&&bitmap.bmHeight==size*2&&bitmap.bmBitsPixel==1,"monochrome scaled mask");
  std::vector<unsigned char> bits(bitmap.bmWidthBytes*bitmap.bmHeight);
  Check(GetBitmapBits(info.hbmMask,LONG(bits.size()),bits.data())==LONG(bits.size()),"read owned bitmap");
  const size_t half=bits.size()/2;bool transparent=false,glyph=false;
  for(size_t i=0;i<half;++i){transparent|=bits[i]!=0;glyph|=bits[i]!=255;}
  Check(transparent&&glyph,"glyph and transparent pixels");
  bool black=true;for(size_t i=half;i<bits.size();++i)black=black&&bits[i]==0;
  Check(black,"black XOR mask");
  DeleteObject(info.hbmMask);if(info.hbmColor)DeleteObject(info.hbmColor);return bits;
}
}
int wmain(){
  try{
    std::atomic<long> objects{0};unsigned toggles=0,settings=0;HRESULT settings_result=S_OK;
    ComPtr<yanvy::ModeLanguageBar> item;
    item.Attach(new yanvy::ModeLanguageBar(GUID_NULL,false,[&]{++toggles;item->SetAscii(true);},[&]{++settings;return settings_result;},&objects));
    Check(objects==1,"item pins module");
    TF_LANGBARITEMINFO info{};Check(item->GetInfo(&info)==S_OK&&info.guidItem==yanvy::kNativeInputModeItem&&
        (info.dwStyle&TF_LBI_STYLE_TEXTCOLORICON)&&(info.dwStyle&TF_LBI_STYLE_BTN_BUTTON),"native item identity and theme style");
    // The official SampleIME advertises SHOWNINTRAY (SDK value 0x2) in GetInfo.
    // A valid button object alone does not request exposure by the system tray.
    Check((info.dwStyle&0x00000002)!=0,"GetInfo advertises native system-tray button");
    ComPtr<ITfSource> source;Check(item->QueryInterface(IID_ITfSource,reinterpret_cast<void**>(source.GetAddressOf()))==S_OK,"ITfSource");
    ComPtr<IUnknown> a,b;Check(item->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(a.GetAddressOf()))==S_OK&&
        source->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(b.GetAddressOf()))==S_OK&&a.Get()==b.Get(),"COM identity");a.Reset();b.Reset();
    ComPtr<Sink> sink;sink.Attach(new Sink);DWORD cookie=TF_INVALID_COOKIE,other=0;
    Check(source->AdviseSink(IID_ITfLangBarItemSink,sink.Get(),&cookie)==S_OK&&cookie!=TF_INVALID_COOKIE,"advise update sink");
    Check(source->AdviseSink(IID_ITfLangBarItemSink,sink.Get(),&other)==CONNECT_E_ADVISELIMIT&&other==TF_INVALID_COOKIE,"only one sink");
    Check(source->AdviseSink(IID_IUnknown,sink.Get(),&other)==CONNECT_E_CANNOTCONNECT,"reject wrong IID");
    Check(source->UnadviseSink(cookie+1)==CONNECT_E_NOCONNECTION,"reject wrong cookie");
    Check(ReadText(item.Get())==L"中","initial Chinese text");
    const auto dpi=GetThreadDpiAwarenessContext();
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{-1200,400},nullptr)==S_OK&&settings==1&&toggles==0,"right opens settings only");
    Check(item->OnClick(TF_LBI_CLK_LEFT,{0,0},nullptr)==S_OK&&toggles==1&&settings==1,"left toggles only");
    Check(ReadText(item.Get())==L"英"&&sink->updates==1&&sink->flags==(TF_LBI_ICON|TF_LBI_TEXT|TF_LBI_TOOLTIP),"mode update refreshes icon text tooltip");
    item->SetAscii(true);Check(sink->updates==1,"unchanged mode skips notification");
    item->SetAscii(false);Check(ReadText(item.Get())==L"中"&&sink->updates==2,"external mode update");
    Check(AreDpiAwarenessContextsEqual(dpi,GetThreadDpiAwarenessContext()),"host DPI unchanged");
    BSTR tooltip=nullptr;Check(item->GetTooltipString(&tooltip)==S_OK&&wcsstr(tooltip,L"右键打开设置"),"discoverable settings tooltip");SysFreeString(tooltip);
    const auto normal_tooltip=ReadTooltip(item.Get());
    settings_result=E_ACCESSDENIED;
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==E_ACCESSDENIED&&settings==2&&toggles==1,"right reports original settings failure without toggling");
    Check(ReadTooltip(item.Get()).find(L"0x80070005")!=std::wstring::npos&&sink->flags==TF_LBI_TOOLTIP,"settings failure exposes only error code in updated tooltip");
    settings_result=HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==settings_result&&ReadTooltip(item.Get()).find(L"0x80070002")!=std::wstring::npos,"retry replaces previous failure code");
    settings_result=S_FALSE;
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==S_FALSE&&ReadTooltip(item.Get())==normal_tooltip&&sink->flags==TF_LBI_TOOLTIP,"successful HRESULT is preserved and retry clears error");
    settings_result=S_OK;
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==S_OK&&ReadTooltip(item.Get())==normal_tooltip&&settings==5,"successful retry remains available");
    DWORD status=0;item->Show(FALSE);Check(item->GetStatus(&status)==S_OK&&(status&TF_LBI_STATUS_HIDDEN),"hidden status");item->Show(TRUE);
    sink->during_update=[&]{Check(source->UnadviseSink(cookie)==S_OK,"reentrant unadvise");};item->SetAscii(true);
    Check(sink->refs==1,"sink released after reentrant notification");sink->during_update={};
    Check(source->UnadviseSink(cookie)==CONNECT_E_NOCONNECTION,"already removed sink");source.Reset();
    HICON first=nullptr,second=nullptr;Check(item->GetIcon(&first)==S_OK&&item->GetIcon(&second)==S_OK&&first!=second,"independently owned icons");DestroyIcon(first);DestroyIcon(second);
    for(int size:{16,24,32,48,64}){
      HICON chinese=yanvy::CreateModeIcon(false,size,size),english=yanvy::CreateModeIcon(true,size,size);
      Check(chinese&&english,"scaled icons created");auto c=IconMask(chinese,size),e=IconMask(english,size);
      Check(c!=e,"Chinese and English glyphs differ");DestroyIcon(chinese);DestroyIcon(english);
    }
    ComPtr<Manager> manager;manager.Attach(new Manager);
    bool cycle_released=false,cycle_release_pinned=false;
    ComPtr<Sink> cycle_sink;cycle_sink.Attach(new Sink);cycle_sink->held=item;
    cycle_sink->during_destroy=[&]{cycle_released=true;cycle_release_pinned=objects==1;};
    Check(item->AdviseSink(IID_ITfLangBarItemSink,cycle_sink.Get(),&manager->sink_cookie)==S_OK,"advise sink retaining item");
    cycle_sink.Reset();
    {
      yanvy::LanguageBarBinding binding;Check(binding.Attach(manager.Get(),item.Get())==S_OK&&manager->adds==1,"AddItem once");
      Check(binding.Attach(manager.Get(),item.Get())==E_UNEXPECTED,"duplicate binding rejected");
      manager->remove_result=E_FAIL;binding.Detach();binding.Detach();
      Check(manager->removes==1,"RemoveItem once despite repeated shutdown");
      Check(cycle_released&&cycle_release_pinned,"disconnect breaks sink/item cycle while pinned");
      Check(manager->unadvise_result==S_OK,"RemoveItem can unadvise disconnected sink");
    }
    const auto stopped_toggles=toggles,stopped_settings=settings;
    Check(item->OnClick(TF_LBI_CLK_LEFT,{0,0},nullptr)==S_FALSE&&item->OnClick(TF_LBI_CLK_RIGHT,{-1,-1},nullptr)==S_FALSE&&toggles==stopped_toggles&&settings==stopped_settings,"shutdown disables callbacks");
    item.Reset();Check(objects==1,"shell-held item still pins module after owner exits");
    ComPtr<ITfLangBarItemButton> retained;Check(manager->held->QueryInterface(IID_ITfLangBarItemButton,reinterpret_cast<void**>(retained.GetAddressOf()))==S_OK,"retained item QI");
    Check(ReadText(retained.Get())==L"英"&&retained->GetIcon(&first)==S_OK,"retained item still safe");DestroyIcon(first);retained.Reset();
    manager->held.Reset();Check(objects==0,"last shell release unpins module");
    bool destructor_released=false,destructor_pinned=false;
    item.Attach(new yanvy::ModeLanguageBar(GUID_NULL,false,[]{},[]{return S_OK;},&objects));
    ComPtr<Sink> destructor_sink;destructor_sink.Attach(new Sink);
    destructor_sink->during_destroy=[&]{destructor_released=true;destructor_pinned=objects==1;};
    Check(item->AdviseSink(IID_ITfLangBarItemSink,destructor_sink.Get(),&cookie)==S_OK,"advise destructor probe sink");
    destructor_sink.Reset();item.Reset();
    Check(destructor_released&&destructor_pinned&&objects==0,"sink Release precedes module unpin");
    item.Attach(new yanvy::ModeLanguageBar(GUID_NULL,false,[&]{++toggles;},[&]{++settings;return S_OK;},&objects));
    manager->add_result=E_FAIL;yanvy::LanguageBarBinding failed;
    Check(failed.Attach(manager.Get(),item.Get())==E_FAIL&&item->OnClick(TF_LBI_CLK_LEFT,{0,0},nullptr)==S_FALSE,"AddItem failure disconnects callbacks");
    item.Reset();Check(objects==0,"failed registration has no module leak");
    item.Attach(new yanvy::ModeLanguageBar(GUID_NULL,false,[]{},[]()->HRESULT{throw std::runtime_error("synthetic settings failure");},&objects));
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==E_FAIL&&ReadTooltip(item.Get()).find(L"0x80004005")!=std::wstring::npos,"settings exception becomes visible HRESULT");
    item.Reset();Check(objects==0,"exception path releases item");
    bool failure_notification_pinned=false;
    item.Attach(new yanvy::ModeLanguageBar(GUID_NULL,false,[]{},[&]{item.Reset();return E_ACCESSDENIED;},&objects));
    ComPtr<Sink> failure_sink;failure_sink.Attach(new Sink);
    failure_sink->during_update=[&]{failure_notification_pinned=objects==1;};
    Check(item->AdviseSink(IID_ITfLangBarItemSink,failure_sink.Get(),&cookie)==S_OK,"advise callback lifetime probe");
    Check(item->OnClick(TF_LBI_CLK_RIGHT,{0,0},nullptr)==E_ACCESSDENIED&&failure_notification_pinned&&objects==0&&failure_sink->refs==1,"failing settings callback may release owner without unloading during notification");
    std::cout<<"Native mode item COM, actions, notifications, theme masks, DPI sizes and lifetime PASS ("<<checks<<" checks)\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}
