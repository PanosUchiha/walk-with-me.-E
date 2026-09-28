#include "windows_memory.h"
#include "identity.h"
#include "supported_btps.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>
#include <fstream>
#include <iterator>
using Guard::Address;
unsigned checks{};
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAILED: %s\n", label); std::exit(1); }
}
constexpr Address target = 0x10000000, wrongTarget = 0x20000000;
std::array<Address,4> nodeTable{0,0,0,0x10100000}, leafTable{0,0,0,0x10200000};
struct alignas(8) Node
{
    std::array<unsigned char,0x128> memory{};
    template <class T> void Put(std::size_t offset, T value) { std::memcpy(memory.data()+offset,&value,sizeof(value)); }
    Node(bool node = true, Address user = 0)
    {
        Put(0, reinterpret_cast<Address>((node ? nodeTable : leafTable).data()));
        Put(8, std::uint32_t{5}); // Refcount, which this guard must never write.
        Put(0xF8, user);
    }
    Address Addr() const { return reinterpret_cast<Address>(memory.data()); }
    void Children(std::vector<Address>& list)
    {
        const auto count = static_cast<std::uint16_t>(list.size());
        Put(0x118, Guard::Children{reinterpret_cast<Address>(list.data()),count,count,count,1});
    }
};
Guard::Context Context() { return {Guard::SafeRead, nodeTable[3], leafTable[3]}; }
bool Find(Node& node, Address object = target, int depth = 0)
{
    auto context = Context();
    return Guard::FindChild(context, node.Addr(), object, depth);
}
bool __cdecl Replacement(void*, void* value, int depth) { return value != nullptr && depth == 2; }

int main(int argc, char** argv)
{
    std::array<unsigned char,32> digest{};
    constexpr std::array<unsigned char,3> abc{'a','b','c'};
    constexpr std::array<unsigned char,32> expectedAbc{0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    Check(Guard::Sha256(abc,digest) && digest==expectedAbc,"SHA-256 matches the standard test vector");
    if (argc>1) {
        std::ifstream file(argv[1],std::ios::binary);
        Check(bool(file),"supported DLL fixture opened");
        std::vector<unsigned char> bytes(std::istreambuf_iterator<char>{file},{});
        Check(bytes.size()==Supported::fileSize && Guard::Sha256(bytes,digest) && digest==Supported::sha256,
              "actual BTPS DLL matches the runtime fingerprint");
        bytes.back()^=1;
        Check(Guard::Sha256(bytes,digest) && digest!=Supported::sha256,"modified BTPS DLL fails identity check");
    }
    Node leaf(false,target), other(false,wrongTarget), root;
    Check(Find(leaf), "direct geometry ownership is preserved");
    Check(!Find(other), "unrelated geometry does not match");
    Check(!Find(root), "empty node does not match");
    auto context = Context();
    Check(!Guard::FindChild(context,0,target), "null nodes stay harmless");
    std::vector<Address> children{0,other.Addr(),leaf.Addr()};
    root.Children(children);
    const auto beforeRoot = root.memory, beforeLeaf = leaf.memory;
    Check(Find(root), "sparse children still find a valid descendant");
    Check(root.memory==beforeRoot && leaf.memory==beforeLeaf, "traversal leaves nodes and refcounts untouched");
    Check(Find(root,target,2), "matching depth-three child is retained");
    Check(!Find(root,target,3), "original recursion cutoff is retained");
    Check(!Find(root,target,-1), "invalid recursion input is rejected");

    // Replay each actual corrupted child value from the September 26 dumps.
    for (Address bad : {Address{0x3F64A983436F7C5C},Address{0x479EEEEF00000000}}) {
        children={bad}; root.Children(children); context=Context();
        Check(!Guard::FindChild(context,root.Addr(),target) && context.invalid, "captured crash child is skipped");
        children={bad,leaf.Addr()}; root.Children(children); context=Context();
        Check(Guard::FindChild(context,root.Addr(),target) && context.invalid, "bad sibling does not disable valid selection");
    }
    auto* inaccessible=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Check(inaccessible!=nullptr,"unreadable fixture allocated");
    children={reinterpret_cast<Address>(inaccessible),leaf.Addr()}; root.Children(children); context=Context();
    Check(Guard::FindChild(context,root.Addr(),target) && context.invalid,"unreadable node cannot crash or hide a valid sibling");
    root.Put(0x118,Guard::Children{reinterpret_cast<Address>(inaccessible),2,2,2,1}); context=Context();
    Check(!Guard::FindChild(context,root.Addr(),target) && context.invalid,"unreadable child table safely fails");
    Check(VirtualFree(inaccessible,0,MEM_RELEASE)!=0,"unreadable fixture released");
    root.Put(0x118,Guard::Children{0,65535,0,0,1}); context=Context();
    Check(!Guard::FindChild(context,root.Addr(),target) && context.invalid,"implausible table header is rejected");
    children={root.Addr()}; root.Children(children); context=Context();
    Check(!Guard::FindChild(context,root.Addr(),target),"cycles stop at the existing depth limit");
    context=Context(); context.budget=1;
    Check(!Guard::FindChild(context,root.Addr(),target) && context.invalid,"work budget is enforced");
    auto unsupported=nodeTable; unsupported[3]=0x10300000; root.Put(0,reinterpret_cast<Address>(unsupported.data())); context=Context();
    Check(!Guard::FindChild(context,root.Addr(),target) && context.unsupported,"unknown virtual overrides are never executed");
    root.Put(0,reinterpret_cast<Address>(nodeTable.data()));

    // Mixed valid, null and malformed entries: only a valid ownership match counts.
    std::mt19937 random(309168315);
    for(unsigned run=0;run<3000;++run) {
        bool expected=false; children.clear();
        for(unsigned i=0;i<32;++i) {
            switch(random()%4) {
            case 0: children.push_back(0); break;
            case 1: children.push_back(other.Addr()); break;
            case 2: children.push_back(0x3F64A983436F7C5C); break;
            default: children.push_back(leaf.Addr()); expected=true; break;
            }
        }
        root.Children(children);
        Check(Find(root)==expected,"mixed-node selection agrees with valid ownership");
    }

    // Exercise the actual absolute jump helper on an isolated executable fixture.
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(code!=nullptr,"code fixture allocated");
    std::array<unsigned char,17> original{}; original.fill(0x90);
    original[14]=0x31; original[15]=0xC0; original[16]=0xC3;
    std::memcpy(code,original.data(),original.size());
    DWORD previous{}; Check(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous)!=0,"code fixture made executable");
    using Function=bool(*)(void*,void*,int); const auto function=reinterpret_cast<Function>(code);
    Check(!function(nullptr,reinterpret_cast<void*>(target),2),"unpatched fixture executes");
    auto mismatch=original; mismatch[0]=0xCC;
    Check(!Guard::InstallJump(code,reinterpret_cast<Address>(&Replacement),mismatch),"foreign patch bytes are rejected");
    Check(std::memcmp(code,original.data(),original.size())==0,"rejected patch leaves code untouched");
    Check(Guard::InstallJump(code,reinterpret_cast<Address>(&Replacement),original),"matching entry can be patched");
    Check(function(nullptr,reinterpret_cast<void*>(target),2),"patched jump preserves arguments and return value");
    Check(!function(nullptr,nullptr,2),"patched function also returns false correctly");
    MEMORY_BASIC_INFORMATION info{}; VirtualQuery(code,&info,sizeof(info));
    Check(info.Protect==PAGE_EXECUTE_READ,"code page protection restored");
    Check(!Guard::InstallJump(code,reinterpret_cast<Address>(&Replacement),original),"double installation is rejected");
    Check(VirtualFree(code,0,MEM_RELEASE)!=0,"code fixture released");
    std::printf("%u checks passed, including both crash-pointer regressions and protected-page tests.\n",checks);
}
