"""Render the actual party menu draw code through ImGui, then rasterize its triangles.

Compiles the header, wheel, page bars, party actions grid, ornament buttons and companion
panel straight out of src/menu_ui.cpp (between their *_BEGIN/*_END markers) together with
src/order_visuals.cpp, drives them through a scripted timeline, and rasterizes the resulting
ImGui triangles with numpy. No game process is opened.

SVG artwork is rasterized either with Node's sharp (--node/--sharp) or headless Edge (--edge).
Requires the ImGui sources in build/menu-audit/imgui, Pillow and numpy.
"""
from pathlib import Path
import argparse, json, subprocess, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cmake', required=True)
p.add_argument('--node')
p.add_argument('--sharp')
p.add_argument('--edge', help='msedge.exe, used instead of Node/sharp to rasterize SVGs')
p.add_argument('--stills', action='store_true', help='render the review stills')
p.add_argument('--gif', action='store_true', help='render the page-change animation')
p.add_argument('--fps', type=int, default=20)
p.add_argument('--custom-title', default='The Silver Ravens', help='sample custom formation name')
p.add_argument('--custom-emblem', default='raven', help='sample Custom::emblems key')
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
out = root / 'build/wheel-visual-proof'
out.mkdir(parents=True, exist_ok=True)
menu = (root / 'src/menu_ui.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')


def block(name, keep_markers=False):
    body = menu.split(f'// {name}_BEGIN: shared with the offline visual proof.', 1)[1].split(f'// {name}_END', 1)[0]
    # Native ImGui returns a reference; the runtime export returns a pointer.
    return body.replace('Im::GetIO()->', 'ImGui::GetIO().')


header = block('PARTY_HEADER')
grid = block('ACTION_GRID')
ornament = block('ORNAMENT_BUTTON')
companion = block('COMPANION_PANEL')
wheel = block('WHEEL_DRAW')
editor = block('FORMATION_EDITOR')
visuals = (root / 'src/order_visuals.cpp').read_text(encoding='utf-8').replace('#include "order_visuals.h"', '')

cpp = r'''
#include "imgui.h"
#include "order_wheel.h"
#include "action_grid.h"
#include "companion_menu.h"
#include "custom_formation.h"
#include "travel_planner.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#include <iostream>
namespace ImGuiMCP {
using namespace ImGui;using ::ImDrawList;using ::ImVec2;using ::ImU32;using ::ImTextureID;
using ::ImGuiHoveredFlags_AllowWhenDisabled;
using ::ImGuiKey_Delete;using ::ImGuiCol_FrameBg;using ::ImGuiCol_FrameBgHovered;using ::ImGuiCol_FrameBgActive;using ::ImGuiCol_Button;using ::ImGuiCol_ButtonHovered;using ::ImGuiCol_PopupBg;using ::ImGuiCol_Header;using ::ImGuiCol_HeaderHovered;using ::ImGuiCol_CheckMark;using ::ImGuiCol_Text;using ::ImGuiCol_Border;using ::ImGuiStyleVar_FrameRounding;using ::ImGuiStyleVar_FramePadding;using ::ImGuiStyleVar_FrameBorderSize;using ::ImGuiKey_LeftArrow;using ::ImGuiKey_RightArrow;using ::ImGuiKey_UpArrow;using ::ImGuiKey_DownArrow;
namespace ImDrawListManager {
void AddText(ImDrawList*d,ImFont*f,float s,ImVec2 p,ImU32 c,const char*t){d->AddText(f,s,p,c,t);}
void AddCircle(ImDrawList*d,ImVec2 p,float r,ImU32 c,int n,float t){d->AddCircle(p,r,c,n,t);}
void AddCircleFilled(ImDrawList*d,ImVec2 p,float r,ImU32 c,int n){d->AddCircleFilled(p,r,c,n);}
void AddLine(ImDrawList*d,ImVec2 a,ImVec2 b,ImU32 c,float t){d->AddLine(a,b,c,t);}
void AddRect(ImDrawList*d,ImVec2 a,ImVec2 b,ImU32 c,float r,int f,float t){d->AddRect(a,b,c,r,f,t);}
void AddRectFilled(ImDrawList*d,ImVec2 a,ImVec2 b,ImU32 c,float r,int f){d->AddRectFilled(a,b,c,r,f);}
void PathArcTo(ImDrawList*d,ImVec2 p,float r,float a,float b,int n){d->PathArcTo(p,r,a,b,n);}
void PathStroke(ImDrawList*d,ImU32 c,int f,float t){d->PathStroke(c,f,t);}
void PathFillConvex(ImDrawList*d,ImU32 c){d->PathFillConvex(c);}
void AddImageQuad(ImDrawList*d,ImTextureID t,ImVec2 a,ImVec2 b,ImVec2 c,ImVec2 e,ImVec2 u,ImVec2 v,ImVec2 w,ImVec2 x,ImU32 col){d->AddImageQuad(t,a,b,c,e,u,v,w,x,col);}
}
}
namespace fmt { inline std::string format(const char* f,const char* a){std::string s=f;auto p=s.find("{}");if(p!=std::string::npos)s.replace(p,2,a);return s;} }
std::vector<std::string> textures{"font"};
namespace SKSEMenuFramework {
ImTextureID LoadTexture(std::string path,ImVec2){
 auto found=std::find(textures.begin(),textures.end(),path);
 if(found!=textures.end())return (ImTextureID)(intptr_t)(found-textures.begin());
 textures.push_back(path);return (ImTextureID)(intptr_t)(textures.size()-1);
}
}
namespace Wayfarer::OrderVisuals {
void Icon(ImDrawList*,ImVec2,float,int,float,float=1,bool=false,ImU32=IM_COL32(226,218,201,255));
enum class Glyph{Target,Link,Power,Formation};
void DrawGlyph(ImDrawList*,ImVec2,float,Glyph,float,ImU32);
void Loot(ImDrawList*,ImVec2,float,bool,float=1);
void Crest(ImDrawList*,ImVec2,float,int,float=1,ImU32=IM_COL32(226,218,201,255));
}
''' + visuals + r'''
ImFont* inscription{};
struct Inscription {Inscription(){ImGui::PushFont(inscription);}~Inscription(){ImGui::PopFont();}};
namespace Wayfarer {
namespace Im=ImGuiMCP;namespace Draw=Im::ImDrawListManager;using V=Im::ImVec2;
constexpr auto gold=IM_COL32(197,180,136,255),muted=IM_COL32(149,146,137,255);
''' + header + grid + ornament + companion + editor + r'''
}
using namespace Wayfarer;
std::array<GridTile,ActionGrid::count> Tiles(int variant,const std::array<bool,ActionGrid::count>& on){
 std::array<GridTile,ActionGrid::count> t;
 t[ActionGrid::Teleport]={"Teleport party",variant==1?"Wait until the scene ends":"3 companions ready",variant!=1};
 t[ActionGrid::TemporaryRest]={"Temporary rest",on[ActionGrid::TemporaryRest]?"Settle when you stop":"Keep to your feet",true,true,on[ActionGrid::TemporaryRest]};
 t[ActionGrid::CompanionControl]={"Companion control",variant==1?"Look at a companion":"Katana",variant!=1};
 t[ActionGrid::Guidance]={"Walk With Me",on[ActionGrid::Guidance]?"Guiding":"Follower AI",true,true,on[ActionGrid::Guidance]};
 t[ActionGrid::HoldHands]={"Hold hands",variant==1?"Choose a partner":"With Rumarin",variant!=1,true,on[ActionGrid::HoldHands]};
 t[ActionGrid::LootScout]={"Loot scouting","Rumarin",true,true,on[ActionGrid::LootScout]};
 t[ActionGrid::Pathfinder]={"Pathfinder",variant==1?"Not in this style":"Curious first",true,true,on[ActionGrid::Pathfinder]};
 t[ActionGrid::BattlePlan]={"Battle plan",variant==1?"Needs Fight With Me":"Roles and tactics",variant!=1};
 return t;
}
// The scripted review timeline (seconds): open on the wheel, go down to the grid, walk the
// selection frame across and down, switch loot scouting off, and come back up to the wheel.
struct Event{float at;int kind;int value;}; // kind: 0 page, 1 tile, 2 press
const std::vector<Event> timeline{{1.0F,0,1},{1.65F,1,1},{2.05F,1,2},{2.45F,1,6},{2.85F,1,5},{3.2F,2,5},{3.85F,1,1},{4.35F,0,0}};
int wheelFocus=0;bool customReady=true;
const std::string customTitle=Custom::CleanTitle(PREVIEW_CUSTOM_TITLE);
const int customEmblem=Custom::EmblemFromKey(PREVIEW_CUSTOM_EMBLEM);
void Scene(float time,int variant,bool pressTimeline,bool gamepad){
 auto& io=ImGui::GetIO();
 Wheel::Motion motion;motion.Open(0);ActionGrid::Motion g;
 std::array<bool,ActionGrid::count> on{false,true,false,true,true,true,true,false};
 g.Reset(0,false,on);
 bool gridPage=false;int tile=0;float elapsed=0,centerElapsed=0;
 const float dt=1.0F/60;
 for(float t=0;t<time;t+=dt){
  if(pressTimeline)for(const auto& e:timeline)if(t<=e.at&&e.at<t+dt){
   if(e.kind==0)gridPage=e.value!=0;else if(e.kind==1)tile=e.value;else{g.Press(e.value);on[e.value]=!on[e.value];}
  }
  if(!pressTimeline){gridPage=variant>=0&&t>=.6F;tile=variant>=0?std::max(0,variant%10):0;}
  motion.Update(dt,t<.5F?0:wheelFocus);g.Update(dt,gridPage,tile,on);elapsed+=dt;centerElapsed=t<.5F?t:t-.5F;
 }
 const auto tiles=Tiles(pressTimeline?0:variant/10,on);
 const float w=650;const V origin{175,50};
 io.AddMousePosEvent(-100,-100);ImGui::NewFrame();
 ImGui::SetNextWindowPos(origin);ImGui::SetNextWindowSize({w,w*Wheel::windowHeight});
 ImGui::Begin("wheel-proof",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
 auto* draw=ImGui::GetWindowDrawList();
 struct {bool enabled=true;int mode=0;bool inGame=true;} view;
 const char* orderNames[]{"Natural","Lead","Companion","Rear","Relax","Vanilla",customTitle.c_str()};
 const int focus=wheelFocus;
 const float windowFade=motion.visibility,page=g.page;
 PartyHeader(draw,origin,w,windowFade,page);
 if(g.WheelVisibility()>.001F){
  const float fade=windowFade*g.WheelVisibility();const float radius=w*.40F*motion.Scale();
  const V center{origin.x+w*.5F,origin.y+w*.48F-w*.05F*page};
''' + wheel + r'''
 }
 static std::array<float,2> hover{};
 if(!gridPage){const auto bar=ActionGrid::MoreBar(w);PageBar("more",{origin.x+bar.x,origin.y+bar.y-w*.05F*page},{bar.w,bar.h},true,"P A R T Y   A C T I O N S",windowFade*g.WheelVisibility(),elapsed,hover[0]);}
 if(page>.005F||gridPage){int hovered=-1;ActionGridPage(origin,w,tiles,tile,g,windowFade*g.GridVisibility(),gridPage,hovered);
  if(gridPage){const auto bar=ActionGrid::BackBar(w);PageBar("back",{origin.x+bar.x,origin.y+bar.y+w*.05F*(1-page)},{bar.w,bar.h},false,"P A R T Y   O R D E R S",windowFade*g.GridVisibility(),elapsed,hover[1]);}}
 const char* wheelHints=gamepad?"Stick: aim    A: order    D-pad down: party actions    B: back":"Click / 1-7: order    Tab: party actions    Esc: back";
 const char* gridHints=gamepad?"Stick / D-pad: choose    A: select    Up: party orders    B: back":"Click / arrows: choose    Enter: select    Tab: party orders    Esc: back";
 CenteredText(draw,wheelHints,{origin.x+w*.5F,origin.y+w*1.235F},w*.017F,Faded(muted,windowFade*g.WheelVisibility()),w*.95F);
 CenteredText(draw,gridHints,{origin.x+w*.5F,origin.y+w*1.235F},w*.017F,Faded(muted,windowFade*g.GridVisibility()),w*.95F);
 ImGui::End();
}
void Emit(){
 ImGui::Render();auto* data=ImGui::GetDrawData();
 std::ofstream file("build/wheel-visual-proof/triangles.bin",std::ios::binary);
 for(int list=0;list<data->CmdListsCount;++list){auto* d=data->CmdLists[list];
  for(const auto& cmd:d->CmdBuffer){if(cmd.UserCallback)continue;uint32_t texture=(uint32_t)(intptr_t)cmd.TextureId;
   for(unsigned i=0;i<cmd.ElemCount;i+=3){file.write((char*)&texture,4);
    for(unsigned k=0;k<3;++k){const auto& v=d->VtxBuffer[d->IdxBuffer[cmd.IdxOffset+i+k]+cmd.VtxOffset];file.write((char*)&v,sizeof(v));}
   }
  }
 }
 std::ofstream manifest("build/wheel-visual-proof/textures.txt");for(auto& t:textures)manifest<<t<<'\n';
}
int main(int argc,char**argv){
 // Each invocation renders a different scene; keep texture IDs identical across them.
 for(const auto* name:{"wheel-slice","natural","arrow","wing-a","wing-b","shield","logs","flame","ember","teleport","chat","loot","pathfinder"})
  SKSEMenuFramework::LoadTexture(std::string("Data/Interface/WalkWithMe/")+name+".svg",{256,256});
 for(const auto& emblem:Custom::emblems)
  SKSEMenuFramework::LoadTexture(std::string("Data/Interface/WalkWithMe/crest-")+emblem.key+".svg",{256,256});
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1000,1000};io.DeltaTime=1.0F/60;io.IniFilename=nullptr;
 io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",24);
 inscription=io.Fonts->AddFontFromFileTTF("package/SKSE/Plugins/Fonts/WalkWithMeInscription.ttf",28);
 unsigned char* pixels;int fw,fh;io.Fonts->GetTexDataAsRGBA32(&pixels,&fw,&fh);
 std::ofstream font("build/wheel-visual-proof/font.bin",std::ios::binary);font.write((char*)&fw,4);font.write((char*)&fh,4);font.write((char*)pixels,fw*fh*4);font.close();
 io.Fonts->SetTexID((ImTextureID)0);
 OrderVisuals::SetCustomEmblem(customEmblem);
 const std::string mode=argc>1?argv[1]:"grid";
 if(mode=="test"){
  int failures=0;auto check=[&](bool pass,const char* what){if(!pass){++failures;std::cerr<<"FAILED: "<<what<<'\n';}};
  // Grid clicks through the real page code: a press and release on a tile selects it once;
  // a disabled tile, a drag off the tile or a click while the wheel is showing selects nothing.
  auto frame=[&](float x,float y,bool down,bool interactive,int variant,int& hovered){
   io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();
   ImGui::SetNextWindowPos({175,50});ImGui::SetNextWindowSize({650,650*Wheel::windowHeight});
   ImGui::Begin("grid-click-test",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
   ActionGrid::Motion g;std::array<bool,ActionGrid::count> on{};g.Reset(0,true,on);
   const int clicked=ActionGridPage({175,50},650,Tiles(variant,on),0,g,1,interactive,hovered);
   ImGui::End();ImGui::Render();return clicked;
  };
  for(int i=0;i<ActionGrid::count;++i){
   const auto b=ActionGrid::TileBox(i,650);const float x=175+b.x+b.w/2,y=50+b.y+b.h/2;int hovered=-1;
   frame(x,y,false,true,0,hovered);frame(x,y,false,true,0,hovered);check(hovered==i,"hover reports the tile");
   frame(x,y,true,true,0,hovered);check(frame(x,y,false,true,0,hovered)==i,"click selects the tile");
   check(frame(x,y,false,true,0,hovered)==-1,"a click selects once");
   frame(x,y,true,true,0,hovered);check(frame(20,20,false,true,0,hovered)==-1,"dragging off cancels");
   frame(x,y,true,false,0,hovered);check(frame(x,y,false,false,0,hovered)==-1,"no clicks while the wheel shows");
  }
  const auto b=ActionGrid::TileBox(ActionGrid::CompanionControl,650);int hovered=-1;
  const float x=175+b.x+b.w/2,y=50+b.y+b.h/2;
  frame(x,y,false,true,1,hovered);frame(x,y,true,true,1,hovered);
  check(frame(x,y,false,true,1,hovered)==-1&&hovered==ActionGrid::CompanionControl,"a disabled tile can be focused but not used");
  std::cout<<"Party actions grid ImGui clicks: "<<failures<<" failures\n";
  ImGui::DestroyContext();return failures?1:0;
 }
 if(mode=="editor"){
  // The formation editor, as the player would find it: variant 0 a drawn formation with Katana
  // and Rumarin placed, a fifth companion falling in behind, and a new place under the mouse;
  // variant 1 the first visit, with nothing drawn yet.
  const int variant=argc>2?std::stoi(argv[2]):0;
  const V at{0,171};const V size{1000,658};
  FormationEditorState s;Custom::Formation f;f.title=customTitle;f.emblem=customEmblem;
  std::vector<EditorCompanion> party{{"katana.esp|005900","Katana"},{"3dnpc.esp|0ab6ec","Rumarin"},{"3dnpc.esp|0ab6d5","Skjel the Gray"},{"lucien.esp|00591f","Lucien Flavius"},{"runtime|003317","M'rissi"}};
  if(variant==0){Custom::Add(f,{-150,100},true);Custom::Add(f,{150,100},true);Custom::Add(f,{-100,-175},true);Custom::Add(f,{100,-175},true);
   Custom::Reserve(f,0,"katana.esp|005900","Katana");Custom::Reserve(f,1,"3dnpc.esp|0ab6ec","Rumarin");}
  s.Open(f);if(variant==0){s.selected=0;}
  const float u=size.y,mapSize=u*.80F;const V centre{at.x+u*.05F+mapSize*.5F,at.y+u*.135F+mapSize*.5F};
  const float scale=(mapSize*.5F-u*.04F)/Custom::reach;
  const V mouse=variant==0?V{centre.x+0*scale,centre.y-350*scale}:V{-100,-100};
  for(int k=0;k<40;++k){io.AddMousePosEvent(mouse.x,mouse.y);ImGui::NewFrame();
   ImGui::SetNextWindowPos(at);ImGui::SetNextWindowSize(size);
   ImGui::Begin("editor-proof",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
   FormationEditor(at,size,s,party,1);ImGui::End();if(k<39)ImGui::Render();}
 Emit();ImGui::DestroyContext();return 0;
 }
 if(mode=="emblems"){
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,1000});
  ImGui::Begin("emblem-proof",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
  auto* draw=ImGui::GetWindowDrawList();
  CenteredText(draw,"CUSTOM FORMATION EMBLEMS",{500,45},27,gold,900);
  for(int i=0;i<Custom::emblemCount;++i){
   const V c{140.0F+(i%4)*240.0F,165.0F+(i/4)*215.0F};
   OrderVisuals::Crest(draw,c,112,i);
   CenteredText(draw,Custom::emblems[i].label,{c.x,c.y+72},22,muted,210);
  }
  ImGui::End();Emit();ImGui::DestroyContext();return 0;
 }
 if(mode=="companion"){
  const int focus=argc>2?std::stoi(argv[2]):6;const float time=argc>3?std::stof(argv[3]):1.5F;
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({200,290});ImGui::SetNextWindowSize({600,600*CompanionMenu::height});
  ImGui::Begin("companion-proof",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav);
  CompanionMenu::Motion panel;for(float t=0;t<time;t+=1.0F/60)panel.Update(1.0F/60);
  if(focus==9){panel.Close();panel.Update(.05F);panel.Update(.05F);}
  panel.buttons[0].hover=time>1?.45F:0;
  CompanionPanel({200,290},600,focus==8?"Greta of Heljarchen Hall":"Lydia",focus==7,focus==6,"Wait until combat ends",panel,false,focus!=8,"Wait until combat ends");
  ImGui::End();Emit();ImGui::DestroyContext();return 0;
 }
 // "timeline <seconds> [pad]" follows the script; "still <variant> <seconds> [pad]" holds one page:
 // variant -1 is the wheel; otherwise tens = tile content (0 normal, 1 unavailable), units = focus.
 if(mode=="still"&&argc>2&&std::stoi(argv[2])<=-2){wheelFocus=6;customReady=std::stoi(argv[2])==-2;}
 const bool timelineMode=mode=="timeline";
 const int variant=timelineMode?0:(argc>2?std::stoi(argv[2]):0);
 const float time=argc>3?std::stof(argv[3]):(timelineMode&&argc>2?std::stof(argv[2]):1.5F);
 const bool pad=argc>4&&std::string(argv[4])=="pad";
 Scene(timelineMode?(argc>2?std::stof(argv[2]):0):time,variant,timelineMode,pad);
 Emit();ImGui::DestroyContext();return 0;
}
'''
cpp = cpp.replace('PREVIEW_CUSTOM_TITLE', json.dumps(args.custom_title)).replace(
    'PREVIEW_CUSTOM_EMBLEM', json.dumps(args.custom_emblem))
(out / 'preview.cpp').write_text(cpp, encoding='utf-8')
imgui = (root / 'build/menu-audit/imgui').as_posix()
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.24)
project(WheelVisualProof LANGUAGES CXX)
add_executable(WheelVisualProof preview.cpp "{root.as_posix()}/src/travel_planner.cpp" "{root.as_posix()}/src/formation_math.cpp" "{imgui}/imgui.cpp" "{imgui}/imgui_draw.cpp" "{imgui}/imgui_widgets.cpp" "{imgui}/imgui_tables.cpp")
target_include_directories(WheelVisualProof PRIVATE "{imgui}" "{root.as_posix()}/include")
target_compile_features(WheelVisualProof PRIVATE cxx_std_20)
target_compile_options(WheelVisualProof PRIVATE /utf-8)
''')
subprocess.run([args.cmake, '-S', str(out), '-B', str(out / 'bin'), '-G', 'Visual Studio 17 2022', '-A', 'x64'], check=True,
               stdout=subprocess.DEVNULL)
subprocess.run([args.cmake, '--build', str(out / 'bin'), '--config', 'Release', '--parallel', '8'], check=True,
               stdout=subprocess.DEVNULL)
exe = out / 'bin/Release/WheelVisualProof.exe'
subprocess.run([str(exe), 'test'], cwd=root, check=True)
subprocess.run([str(exe), 'still', '-1', '1.5'], cwd=root, check=True)  # writes textures.txt

svg_paths = [root / 'package' / name.removeprefix('Data/') for name in (out / 'textures.txt').read_text().splitlines()[1:]]
if args.edge:
    for i, svg in enumerate(svg_paths):
        png = out / f'texture-{i}.png'
        stamp = out / f'texture-{i}.source'
        if not stamp.exists() or stamp.read_text() != str(svg):
            png.unlink(missing_ok=True)  # Cached from a run with a different texture order.
        stamp.write_text(str(svg))
        if png.exists() and png.stat().st_mtime > svg.stat().st_mtime:
            continue
        page = out / f'texture-{i}.html'
        page.write_text(f'<html><body style="margin:0;background:transparent"><img src="{svg.as_uri()}" '
                        'style="width:768px;height:768px;display:block"></body></html>', encoding='utf-8')
        subprocess.run([args.edge, '--headless=new', '--disable-gpu', '--hide-scrollbars',
                        '--default-background-color=00000000', '--window-size=768,768', f'--screenshot={png}',
                        page.as_uri()], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
else:
    (out / 'raster-assets.cjs').write_text('const sharp=require(' + json.dumps(args.sharp) + ');\nPromise.all(' + json.dumps(
        [str(s) for s in svg_paths]) + '.map((p,i)=>sharp(p,{density:288}).resize(768,768).png().toFile(__dirname+"/texture-"+i+".png"))).catch(e=>{console.error(e);process.exit(1)});')
    subprocess.run([args.node, str(out / 'raster-assets.cjs')], check=True)

font = (out / 'font.bin').read_bytes()
fw, fh = struct.unpack_from('<ii', font)
textures = [np.frombuffer(font[8:], dtype=np.uint8).reshape(fh, fw, 4).astype(np.float32)]
textures += [np.asarray(Image.open(out / f'texture-{i}.png').convert('RGBA')).astype(np.float32) for i in range(len(svg_paths))]


def render(name, crop=None):
    raw = (out / 'triangles.bin').read_bytes()
    dtype = np.dtype([('x', '<f4'), ('y', '<f4'), ('u', '<f4'), ('v', '<f4'), ('col', '<u4')])
    canvas = np.zeros((1000, 1000, 3), dtype=np.float32)
    yy, xx = np.mgrid[:1000, :1000]
    glow = np.maximum(0, 1 - np.hypot((xx - 500) / 650, (yy - 370) / 650))
    for c, base in enumerate((13, 20, 25)):
        canvas[:, :, c] = base + glow * (8 + c * 2)
    # The in-game backdrop dims the world by 165/255 behind the window.
    for at in range(0, len(raw), 64):
        texture = struct.unpack_from('<I', raw, at)[0]
        v = np.frombuffer(raw, dtype=dtype, count=3, offset=at + 4)
        x0 = max(0, int(np.floor(min(v['x'])))); x1 = min(999, int(np.ceil(max(v['x']))))
        y0 = max(0, int(np.floor(min(v['y'])))); y1 = min(999, int(np.ceil(max(v['y']))))
        if x0 > x1 or y0 > y1:
            continue
        den = (v['y'][1] - v['y'][2]) * (v['x'][0] - v['x'][2]) + (v['x'][2] - v['x'][1]) * (v['y'][0] - v['y'][2])
        if abs(den) < 1e-6:
            continue
        y, x = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        x = x + .5; y = y + .5
        a = ((v['y'][1] - v['y'][2]) * (x - v['x'][2]) + (v['x'][2] - v['x'][1]) * (y - v['y'][2])) / den
        b = ((v['y'][2] - v['y'][0]) * (x - v['x'][2]) + (v['x'][0] - v['x'][2]) * (y - v['y'][2])) / den
        weights = np.stack([a, b, 1 - a - b], axis=-1)
        mask = np.all(weights >= -1e-4, axis=-1)
        if not mask.any():
            continue
        colors = np.stack([(v['col'] >> shift) & 255 for shift in (0, 8, 16, 24)], axis=-1).astype(np.float32)
        tex = textures[texture] if texture < len(textures) else None
        if tex is None:
            continue
        th, tw = tex.shape[:2]
        if texture == 0 and np.ptp(v['u']) < 1e-7 and np.ptp(v['v']) < 1e-7:
            sample = tex[min(th - 1, int(v['v'][0] * th)), min(tw - 1, int(v['u'][0] * tw))][None, None, :]
        else:
            u = np.clip(weights @ v['u'] * tw - .5, 0, tw - 1); t = np.clip(weights @ v['v'] * th - .5, 0, th - 1)
            ix = u.astype(int); iy = t.astype(int); fx = (u - ix)[..., None]; fy = (t - iy)[..., None]
            ix1 = np.minimum(ix + 1, tw - 1); iy1 = np.minimum(iy + 1, th - 1)
            sample = (tex[iy, ix] * (1 - fx) + tex[iy, ix1] * fx) * (1 - fy) + (tex[iy1, ix] * (1 - fx) + tex[iy1, ix1] * fx) * fy
        rgba = sample * (weights @ colors) / 255
        alpha = (rgba[:, :, 3] / 255 * mask)[..., None]
        dest = canvas[y0:y1 + 1, x0:x1 + 1]
        dest[:] = rgba[:, :, :3] * alpha + dest * (1 - alpha)
    image = Image.fromarray(canvas.clip(0, 255).astype(np.uint8))
    if crop:
        image = image.crop(crop)
    image.save(out / name)
    return image


if args.stills:
    for label, variant, time, extra in (('wheel', -1, 1.5, []), ('grid', 0, 1.9, []), ('grid-hold-hands', 4, 2.2, ['pad']),
                                        ('grid-pathfinder', 6, 2.2, ['pad']),
                                        ('grid-unavailable', 12, 2.2, [])):
        subprocess.run([str(exe), 'still', str(variant), str(time), *extra], cwd=root, check=True)
        render(f'party-{label}.png', (150, 30, 850, 960))
    for label, variant in (('wheel-custom', -2), ('wheel-custom-unavailable', -3)):
        subprocess.run([str(exe), 'still', str(variant), '2.2'], cwd=root, check=True)
        render(f'party-{label}.png', (150, 30, 850, 960))
    for label, variant in (('formation-editor', 0), ('formation-editor-empty', 1)):
        subprocess.run([str(exe), 'editor', str(variant)], cwd=root, check=True)
        render(f'{label}.png', (0, 150, 1000, 850))
    subprocess.run([str(exe), 'emblems'], cwd=root, check=True)
    render('formation-emblems.png')
    for label, time in (('transition', 1.12), ('frame-glide', 1.73), ('toggle', 3.28)):
        subprocess.run([str(exe), 'timeline', str(time)], cwd=root, check=True)
        render(f'party-{label}.png', (150, 30, 850, 960))
    sheet = Image.new('RGB', (1800, 835), (13, 20, 25))
    ink = ImageDraw.Draw(sheet)
    heading = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 29)
    caption = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 18)
    ink.text((30, 18), 'Walk With Me - party actions and custom formation', font=heading, fill=(226, 218, 201))
    ink.text((30, 60), 'Offline renders of the current draw code. Sample name and emblem shown; game settings are unchanged.',
             font=caption, fill=(149, 146, 137))
    for name, label, x, width, crop in (
            ('party-grid-pathfinder.png', 'Seven actions in a 4 + 3 grid', 20, 550, (0, 0, 700, 750)),
            ('party-wheel-custom.png', 'Custom name and Raven emblem', 590, 550, (0, 0, 700, 770)),
            ('formation-emblems.png', 'All 16 formation emblems', 1160, 620, None)):
        panel = Image.open(out / name)
        if crop:
            panel = panel.crop(crop)
        panel = panel.resize((width, round(panel.height * width / panel.width)), Image.Resampling.LANCZOS)
        sheet.paste(panel, (x, 135))
        ink.text((x + 12, 102), label, font=caption, fill=(197, 180, 136))
    sheet.save(out / 'roles-custom-review.png')
if args.gif:
    frames = []
    for k in range(int(4.9 * args.fps)):
        subprocess.run([str(exe), 'timeline', f'{k / args.fps:.4f}'], cwd=root, check=True)
        frames.append(render('frame.png', (150, 30, 850, 960)).resize((560, 744), Image.LANCZOS))
    frames[0].save(out / 'party-menu.gif', save_all=True, append_images=frames[1:], duration=int(1000 / args.fps), loop=0)
print(out)
