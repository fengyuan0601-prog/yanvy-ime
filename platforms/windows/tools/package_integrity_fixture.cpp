// SPDX-License-Identifier: BSD-3-Clause
// Fresh synthetic directories only; no production registration or execution.
#include "package_integrity.hpp"
#include "installer_ui.hpp"
#include "installer_transaction.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <winioctl.h>
namespace {
unsigned checks=0;
void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
constexpr char digest[]="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
std::string Line(char scope,const char* name){return std::string(1,scope)+"\t"+digest+"\t"+name+"\n";}
std::string ManifestText(){return std::string("YANVY-PACKAGE-MANIFEST 1\n")+
    Line('D',"data/default.yaml")+Line('D',"profile/default.custom.yaml")+
    Line('D',"licenses/中文说明.txt")+Line('D',"BUILD-INFO.json")+
    Line('D',"YANVY-INSTALL-MARKER.txt")+Line('R',"YANVY-Server.exe");}
void Write(const std::filesystem::path& path,const std::string& bytes="abc"){
  std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;if(!out)throw std::runtime_error("write fixture file");
}
void Populate(const std::filesystem::path& root,const yanvy::PackageManifest& manifest,bool data_only){
  for(const auto& file:manifest)if(!data_only||file.data)Write(root/file.relative);
}
bool Junction(const std::filesystem::path& link,const std::filesystem::path& target){
  if(!CreateDirectoryW(link.c_str(),nullptr))return false;
  HANDLE directory=CreateFileW(link.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(directory==INVALID_HANDLE_VALUE)return false;
  struct Buffer{DWORD tag;WORD length,reserved,sub_offset,sub_length,print_offset,print_length;wchar_t paths[32768];} data{};
  const auto print=target.wstring(),sub=L"\\??\\"+print;data.tag=IO_REPARSE_TAG_MOUNT_POINT;
  data.sub_length=WORD(sub.size()*2);data.print_offset=WORD((sub.size()+1)*2);data.print_length=WORD(print.size()*2);
  memcpy(data.paths,sub.c_str(),(sub.size()+1)*2);memcpy(reinterpret_cast<BYTE*>(data.paths)+data.print_offset,print.c_str(),(print.size()+1)*2);
  DWORD bytes=DWORD(offsetof(Buffer,paths)+(sub.size()+print.size()+2)*2),returned=0;data.length=WORD(bytes-8);
  const bool ok=DeviceIoControl(directory,FSCTL_SET_REPARSE_POINT,&data,bytes,nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(directory);return ok;
}
DWORD ProbeTransaction(const std::wstring& name){
  wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,DWORD(std::size(self)));
  std::wstring command=L"\""+std::wstring(self)+L"\" --lock-probe \""+name+L"\"";
  STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
  if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return GetLastError();
  CloseHandle(process.hThread);DWORD result=ERROR_TIMEOUT;
  if(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&result);
  CloseHandle(process.hProcess);return result;
}
}
int wmain(int argc,wchar_t** argv){
  try{
    if(argc==3&&std::wstring_view(argv[1])==L"--lock-probe"){yanvy::InstallerTransaction lock;return int(lock.Acquire(argv[2]));}
    if(argc!=2)return 1;
    const auto base=std::filesystem::path(argv[1]);
    Check(base.is_absolute()&&base.lexically_normal()==base&&base!=base.root_path()&&!std::filesystem::exists(base)&&yanvy::PackagePathWithoutReparse(base),"fresh explicit fixture root");
    std::filesystem::create_directories(base);
    yanvy::PackageManifest manifest;
    Check(yanvy::ParsePackageManifest(ManifestText(),manifest)&&manifest.size()==6,"trusted manifest format and UTF-8");
    const auto parsed=manifest;
    std::string windows_text;for(char c:ManifestText()){if(c=='\n')windows_text+='\r';windows_text+=c;}
    Check(yanvy::ParsePackageManifest(windows_text,manifest)&&manifest.size()==6,"Windows CRLF manifest resource accepted");
    for(const auto& bad:{std::string(),std::string("YANVY-PACKAGE-MANIFEST 2\n"),ManifestText()+Line('R',"../outside"),ManifestText()+Line('R',"C:/outside"),ManifestText()+Line('R',"data\\bad.yaml"),ManifestText()+Line('R',"DATA/DEFAULT.YAML"),ManifestText()+Line('D',"licenses/extra.EXE"),ManifestText()+Line('D',"data/extra.lua"),ManifestText()+"R\tbad\tfile\n"})
      Check(!yanvy::ParsePackageManifest(bad,manifest),"malformed or unsafe manifest refused");
    manifest=parsed;
    const auto empty=base/L"empty";
    Check(yanvy::InspectPackageDestination(empty,manifest,true)==yanvy::PackageDestination::empty,"missing destination is new");
    std::filesystem::create_directories(empty);
    Check(yanvy::InspectPackageDestination(empty,manifest,true)==yanvy::PackageDestination::empty,"existing empty destination is new");
    const auto data=base/L"中文 词库";Populate(data,manifest,true);
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::reusable,"complete data payload reused without executables");
    const auto current=base/L"runtime"/L"current",prior=base/L"runtime"/L"prior",fallback=base/L"default";
    Check(yanvy::SelectExistingInstallData(fallback,current,current,data,true)==data,"same-runtime verified repair retains custom data location");
    Check(yanvy::SelectExistingInstallData(fallback,prior,current,data,true)==fallback,"new version keeps its new default location");
    Check(yanvy::SelectExistingInstallData(fallback,current,current,data,false)==fallback,"unverified previous data does not become default");
    Check(yanvy::ResolveSelectedInstallPath(data,false,true)==data,"browse keeps full existing custom package path");
    Check(yanvy::ResolveSelectedInstallPath(empty,true,false)==empty,"browse keeps full empty folder path");
    Check(yanvy::ResolveSelectedInstallPath(base,false,false)==yanvy::ResolveInstallPath(base),"unrelated populated folder keeps dedicated child behavior");
    const auto lock_name=L"Global\\YANVY.Setup.User.Fixture."+base.filename().wstring();
    yanvy::InstallerTransaction first,second;
    Check(first.Acquire(lock_name)==0,"first UI holds transaction through prepare and UAC");
    Check(second.Acquire(lock_name)==ERROR_BUSY&&ProbeTransaction(lock_name)==ERROR_BUSY,"concurrent UI and other process cannot acquire data ownership");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::reusable,"blocked second attempt leaves prepared files intact");
    first.Release();Check(ProbeTransaction(lock_name)==0&&second.Acquire(lock_name)==0,"failed attempt completion releases marker for retry");
    second.Release();Check(first.Acquire(lock_name)==0,"retry can reacquire after prior cleanup");first.Release();
    Check(yanvy::InstallationReferencesData(data,data)&&!yanvy::InstallationReferencesData(data,fallback),"committed installation blocks rollback only for referenced data");
    Check(yanvy::InspectPackageDestination(data,manifest,false)==yanvy::PackageDestination::rejected,"data payload never authorizes runtime reuse");
    Write(data/L"settings.ini","personal settings");Write(data/L"private"/L"notes.txt","unknown root file");Write(data/L"licenses"/L"extra.txt","unknown license");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::reusable,"unknown root and license files preserved");
    Write(data/L"data"/L"default.yaml","modified input");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"altered input refused");Write(data/L"data"/L"default.yaml");
    Write(data/L"YANVY-INSTALL-MARKER.txt","YANVY another commit");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"different package marker refused");Write(data/L"YANVY-INSTALL-MARKER.txt");
    Write(data/L"BUILD-INFO.json","forged own hash manifest");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"installed BUILD-INFO never supplies trusted hashes");Write(data/L"BUILD-INFO.json");
    Check(DeleteFileW((data/L"profile"/L"default.custom.yaml").c_str())!=FALSE,"remove own fixture input");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"missing fixed input refused");Write(data/L"profile"/L"default.custom.yaml");
    Write(data/L"profile"/L"extra.custom.yaml","unexpected policy");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"extra profile input refused");
    Check(DeleteFileW((data/L"profile"/L"extra.custom.yaml").c_str())!=FALSE,"remove own extra fixture input");
    std::filesystem::create_directory(data/L"data"/L"unexpected");
    Check(yanvy::InspectPackageDestination(data,manifest,true)==yanvy::PackageDestination::rejected,"extra input directory refused");
    Check(RemoveDirectoryW((data/L"data"/L"unexpected").c_str())!=FALSE,"remove own empty fixture directory");
    const auto runtime=base/L"machine";Populate(runtime,manifest,false);Write(runtime/L"profile"/L"build"/L"cached.bin","generated runtime cache");
    HANDLE loaded=CreateFileW((runtime/L"YANVY-Server.exe").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(loaded!=INVALID_HANDLE_VALUE,"hold simulated loaded runtime file");
    Check(yanvy::InspectPackageDestination(runtime,manifest,false)==yanvy::PackageDestination::reusable,"matching locked runtime reused without replacing bytes");CloseHandle(loaded);
    Write(runtime/L"YANVY-Server.exe","changed code");
    Check(yanvy::InspectPackageDestination(runtime,manifest,false)==yanvy::PackageDestination::rejected,"changed runtime code refused");
    const auto link=base/L"linked-data";Check(Junction(link,data),"create isolated junction");
    Check(yanvy::InspectPackageDestination(link,manifest,true)==yanvy::PackageDestination::rejected,"destination junction refused");
    Check(!yanvy::PackageFileDigest(link/L"data"/L"default.yaml",manifest[0].digest),"ancestor junction refused for file hash");
    const auto inner=base/L"inner-link";Populate(inner,parsed,true);
    Check(DeleteFileW((inner/L"data"/L"default.yaml").c_str())&&RemoveDirectoryW((inner/L"data").c_str()),"remove own isolated input folder");
    Check(Junction(inner/L"data",data/L"data"),"create isolated input junction");
    Check(yanvy::InspectPackageDestination(inner,parsed,true)==yanvy::PackageDestination::rejected,"input subtree junction refused");
    Check(yanvy::PreparedDataAccepted(0)&&yanvy::PreparedDataOwnedByAttempt(0),"new data acquires rollback ownership");
    Check(yanvy::PreparedDataAccepted(yanvy::kDataReused)&&!yanvy::PreparedDataOwnedByAttempt(yanvy::kDataReused),"reused data never acquires rollback ownership after UAC cancel");
    Check(!yanvy::PreparedDataAccepted(yanvy::kPackageMismatch)&&!yanvy::PreparedDataOwnedByAttempt(yanvy::kPackageMismatch),"failed validation has no cleanup ownership");
    Check(!yanvy::MayCleanupPreviousInstallation(false,false,false),"old data retained before new broker verification");
    Check(!yanvy::MayCleanupPreviousInstallation(false,true,true),"stop or verification failure retains previous data");
    Check(yanvy::MayCleanupPreviousInstallation(false,false,true),"successful handoff permits old data cleanup");
    Check(yanvy::MayCleanupPreviousInstallation(true,true,false),"uninstall preserves existing cleanup behavior");
    Check(std::filesystem::exists(data/L"settings.ini")&&std::filesystem::exists(data/L"private"/L"notes.txt")&&std::filesystem::exists(data/L"licenses"/L"extra.txt"),"all rejected attempts leave unknown files intact");
    std::cout<<"Package integrity and reuse ownership PASS ("<<checks<<" checks)\n";return 0;
  }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
