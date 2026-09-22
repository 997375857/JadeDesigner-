#define NOMINMAX
#include "../src/DesignWorkspaceStore.h"
#include <cassert>
#include <iostream>

int main() {
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);
    const auto project=std::wstring(temp)+L"jade-design-test-"+std::to_wstring(GetCurrentProcessId())+L".e";
    const auto path=DesignWorkspaceStore::Path(project);
    DeleteFileW(path.c_str());
    std::string bytes,error;
    assert(DesignWorkspaceStore::Read(path,bytes,error)&&bytes.empty());
    assert(DesignWorkspaceStore::Save(path,"","{\"version\":1}",error));
    assert(DesignWorkspaceStore::Read(path,bytes,error)&&bytes=="{\"version\":1}");
    assert(!DesignWorkspaceStore::Save(path,"","bad",error));
    assert(!DesignWorkspaceStore::Save(path,bytes,std::string(1024*1024+1,'a'),error));
    assert(!DesignWorkspaceStore::Save(path,bytes,std::string("a\0b",3),error));
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    assert(lock!=INVALID_HANDLE_VALUE);
    assert(!DesignWorkspaceStore::Save(path,bytes,"new",error));CloseHandle(lock);
    assert(DesignWorkspaceStore::Save(path,bytes,"{}",error));
    assert(DesignWorkspaceStore::Read(path,bytes,error)&&bytes=="{}");
    assert(DesignWorkspaceStore::Path(project,true)!=path);
    DeleteFileW(path.c_str());
    std::cout<<"Design sidecar: create, update, conflict, lock, limits passed\n";
}
