#include "editor_ui.h"
#include "graph_ui.h"
#include "material_ui.h"
#include "ui_icons.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace atelier {
namespace {
struct Rect { float x,y,w,h; ImVec2 Min() const{return {x,y};} ImVec2 Max() const{return {x+w,y+h};} };
const ImU32 BG=IM_COL32(23,28,32,255), PANEL=IM_COL32(28,33,38,255), RAISED=IM_COL32(37,43,49,255);
const ImU32 BORDER=IM_COL32(49,57,64,255), TEXT=IM_COL32(193,200,208,255), MUTED=IM_COL32(128,139,150,255);
const ImU32 CYAN=IM_COL32(102,186,208,255), BLUE=IM_COL32(58,126,188,255), SELECT=IM_COL32(43,72,88,255);
Texture ref,sceneTexture; ImDrawList* dl; float s=1; int serial=100;
ImFont* regular=nullptr;
struct Node {
    int id,parent; std::string name; Icon icon; bool folder=false,open=true,visible=true,locked=false;
    float position[3]={12.843f,5.129f,2.317f},rotation[3]={0,90,0},scale[3]={1,1,1};
    int mesh=0,material[2]={0,1},mobility=0,lod=0; bool cast=true,receive=true,inGame=true;
    float distance=10000,mass=20,friction=.5f; bool collision=true,physics=false,gravity=true,tick=true;
    std::vector<std::string> components,tags={"ruins"};
};
struct Asset {const char* name; const char* folder; const char* type; float x,y,w,h; int triangles,vertices;};
const Asset assets[]={
 {"SM_Archway_A","Ruins","Static Mesh",340,529,49,52,12436,6872},
 {"SM_Pillar_A","Ruins","Static Mesh",404,529,46,52,5284,2960},
 {"SM_Pillar_B","Ruins","Static Mesh",465,529,46,52,6128,3420},
 {"SM_Stairs_A","Ruins","Static Mesh",525,529,46,52,3816,2048},
 {"SM_Wall_A","Ruins","Static Mesh",587,529,46,52,7842,4320},
 {"SM_Rubble_A","Ruins","Static Mesh",646,529,46,52,2936,1690},
 {"SM_Rock_A","Nature","Static Mesh",707,529,46,52,4132,2410},
 {"SM_Rock_B","Nature","Static Mesh",769,529,46,52,3876,2090},
 {"SM_Crate_A","Props","Static Mesh",341,612,46,54,1248,864},
 {"SM_Barrel_A","Props","Static Mesh",405,612,46,54,2680,1542},
 {"SM_Banner_A","Props","Static Mesh",465,612,46,54,1804,1132},
 {"SM_Broken_Column","Ruins","Static Mesh",526,612,46,54,4096,2216},
 {"SM_Stone_Floor","Ruins","Static Mesh",587,612,46,54,2840,1420},
 {"SM_Cliff_A","Nature","Static Mesh",647,612,46,54,9024,4980},
 {"SM_Cliff_B","Nature","Static Mesh",707,612,46,54,8640,4672},
 {"SM_Olive_Tree_A","Nature","Static Mesh",769,612,46,54,18762,10640}
};
struct Snapshot {std::vector<Node> nodes;int selected;int nextId;};
std::vector<Node> nodes; std::vector<Snapshot> past,future; Snapshot editBefore;
int selected=10,selectedAsset=0,sceneTab=0,inspectorTab=0,contentTab=0,tool=0,space=0,perspective=0,viewMode=0;
int folder=-1,assetFilter=0; bool favorites=false,listMode=false,playing=false,snap=false,grid=false,stats=true,focusView=false;
bool expanded[9]={true,true,true,true,false,false,false,true,true};
float leftWidth=198,rightWidth=270,bottomHeight=253,thumbnail=56,cameraSpeed=1,zoom=1,panX=0,panY=0;
float exposure=0,sunTime=17.4f,sunIntensity=3.5f,wind=.3f; bool ambient=true,worldFog=true;
float animationTime=0,animationDuration=10; bool animationLoop=true;
char sceneSearch[128]={},assetSearch[128]={},propertySearch[128]={},tagText[64]={},sceneName[128]="Ancient_Coast";
char renameText[128]={};int renaming=-1; bool dirty=false; std::string toast; double toastUntil=0;
bool wantAbout=false,wantHelp=false,wantAsset=false,wantReset=false,wantPicker=false;int pickerTarget=0;
int activeWorkspace=2; // 0 Scene, 1 Graph, 2 Materials.
std::string statePath="Ancient_Coast.atelier";
struct GraphNode {std::string label;ImVec2 p;int kind;};
std::vector<GraphNode> graph={{"On Begin Play",{25,38},0},{"Rotate Archway",{219,74},1},{"Set Visibility",{412,38},2}};
bool graphConnected=true;

std::string Lower(std::string t){for(auto& c:t)c=(char)std::tolower((unsigned char)c);return t;}
bool Matches(const std::string& t,const char* query){return !*query||Lower(t).find(Lower(query))!=std::string::npos;}
Node* Current(){for(auto& n:nodes)if(n.id==selected)return &n;return nullptr;}
Snapshot Capture(){return {nodes,selected,serial};}
void Remember(){past.push_back(Capture());if(past.size()>100)past.erase(past.begin());future.clear();dirty=true;}
void Notify(const std::string& t){toast=t;toastUntil=ImGui::GetTime()+3.5;}
void Undo(){if(past.empty())return;future.push_back(Capture());auto q=past.back();past.pop_back();nodes=q.nodes;selected=q.selected;serial=q.nextId;dirty=true;Notify("Undo");}
void Redo(){if(future.empty())return;past.push_back(Capture());auto q=future.back();future.pop_back();nodes=q.nodes;selected=q.selected;serial=q.nextId;dirty=true;Notify("Redo");}
void TrackEdit(){if(ImGui::IsItemActivated())editBefore=Capture();if(ImGui::IsItemDeactivatedAfterEdit()){past.push_back(editBefore);future.clear();dirty=true;}}
void Defaults(){
 nodes.clear();serial=100;
 auto add=[](int id,int parent,const char* name,Icon icon,bool folder=false,bool open=true,int mesh=0){Node n;n.id=id;n.parent=parent;n.name=name;n.icon=icon;n.folder=folder;n.open=open;n.mesh=mesh;nodes.push_back(n);};
 add(0,-1,"Ancient_Coast",Icon::Scene,true);add(1,0,"Environment",Icon::Folder,true);add(2,1,"Terrain",Icon::Cube);
 add(3,1,"Rocks",Icon::Folder,true);add(4,3,"Rock_Large_A",Icon::Cube,false,true,6);add(5,3,"Rock_Large_B",Icon::Cube,false,true,7);add(6,3,"Rock_Small_A",Icon::Cube,false,true,5);
 add(7,1,"Vegetation",Icon::Folder,true);add(8,7,"Tree_Olive_A",Icon::Cube,false,true,15);add(9,7,"Tree_Olive_B",Icon::Cube,false,true,15);add(21,7,"Bush_A",Icon::Cube,false,true,15);add(22,7,"Grass_Clump",Icon::Cube,false,true,15);
 add(11,0,"Structures",Icon::Folder,true);add(12,11,"Ruins",Icon::Folder,true);add(10,12,"Archway",Icon::Cube);add(13,12,"Pillar_A",Icon::Cube,false,true,1);add(14,12,"Pillar_B",Icon::Cube,false,true,2);add(15,12,"Stairs",Icon::Cube,false,true,3);add(16,12,"Wall_Fragment",Icon::Cube,false,true,4);
 add(17,0,"Props",Icon::Folder,true,false);add(23,17,"Crate",Icon::Cube,false,true,8);add(24,17,"Barrel",Icon::Cube,false,true,9);
 add(18,0,"Lighting",Icon::Folder,true,false);add(25,18,"DirectionalLight",Icon::Sun);add(26,18,"SkyLight",Icon::Light);
 add(19,0,"Gameplay",Icon::Folder,true);add(20,19,"PlayerStart",Icon::Person);add(27,19,"CameraPoints",Icon::Folder,true,false);add(28,19,"Triggers",Icon::Folder,true,false);add(29,19,"Audio",Icon::Folder,true,false);add(30,19,"VFX",Icon::Folder,true,false);
 selected=10;selectedAsset=0;past.clear();future.clear();dirty=false;
}
void Save(){
 std::ofstream out(std::filesystem::u8path(statePath));if(!out){Notify("Cannot write scene file");return;}
 out<<"ATELIER_SCENE 1\n"<<std::quoted(sceneName)<<' '<<selected<<' '<<serial<<'\n';
 out<<leftWidth<<' '<<rightWidth<<' '<<bottomHeight<<' '<<exposure<<' '<<sunTime<<' '<<sunIntensity<<' '<<wind<<' '<<ambient<<' '<<worldFog<<'\n';
 out<<nodes.size()<<'\n';
 for(const auto& n:nodes){out<<n.id<<' '<<n.parent<<' '<<std::quoted(n.name)<<' '<<(int)n.icon<<' '<<n.folder<<' '<<n.open<<' '<<n.visible<<' '<<n.locked<<' '<<n.mesh<<' '<<n.material[0]<<' '<<n.material[1]<<' '<<n.mobility<<' '<<n.lod<<' '<<n.cast<<' '<<n.receive<<' '<<n.inGame<<' '<<n.distance<<' '<<n.mass<<' '<<n.friction<<' '<<n.collision<<' '<<n.physics<<' '<<n.gravity<<' '<<n.tick<<'\n';for(float f:n.position)out<<f<<' ';for(float f:n.rotation)out<<f<<' ';for(float f:n.scale)out<<f<<' ';out<<'\n'<<n.components.size()<<' ';for(const auto& v:n.components)out<<std::quoted(v)<<' ';out<<'\n'<<n.tags.size()<<' ';for(const auto& v:n.tags)out<<std::quoted(v)<<' ';out<<'\n';}
 dirty=false;Notify("Saved Ancient_Coast.atelier");
}
void Load(){
 std::ifstream in(std::filesystem::u8path(statePath));std::string magic,name;int version=0;in>>magic>>version;if(magic!="ATELIER_SCENE"||version!=1){Notify("No valid Ancient_Coast.atelier file found");return;}
 int sel,ser;float lw,rw,bh,ex,st,si,wi;bool am,fo;in>>std::quoted(name)>>sel>>ser>>lw>>rw>>bh>>ex>>st>>si>>wi>>am>>fo;
 size_t count=0;in>>count;if(count>10000){Notify("Invalid scene file");return;}std::vector<Node> loaded;
 for(size_t i=0;i<count&&in;++i){Node n;int icon;in>>n.id>>n.parent>>std::quoted(n.name)>>icon>>n.folder>>n.open>>n.visible>>n.locked>>n.mesh>>n.material[0]>>n.material[1]>>n.mobility>>n.lod>>n.cast>>n.receive>>n.inGame>>n.distance>>n.mass>>n.friction>>n.collision>>n.physics>>n.gravity>>n.tick;for(auto& f:n.position)in>>f;for(auto& f:n.rotation)in>>f;for(auto& f:n.scale)in>>f;n.icon=(Icon)icon;n.mesh=std::clamp(n.mesh,0,15);n.material[0]=std::clamp(n.material[0],0,2);n.material[1]=std::clamp(n.material[1],0,2);size_t c=0;in>>c;if(c>100){Notify("Invalid components");return;}n.components.clear();for(size_t j=0;j<c;++j){std::string q;in>>std::quoted(q);n.components.push_back(q);}in>>c;if(c>100){Notify("Invalid tags");return;}n.tags.clear();for(size_t j=0;j<c;++j){std::string q;in>>std::quoted(q);n.tags.push_back(q);}loaded.push_back(n);}
 if(!in||loaded.empty()){Notify("Incomplete scene file");return;}Remember();nodes=loaded;selected=sel;serial=ser;std::snprintf(sceneName,sizeof(sceneName),"%s",name.c_str());leftWidth=std::clamp(lw,150.f,330.f);rightWidth=std::clamp(rw,240.f,410.f);bottomHeight=std::clamp(bh,190.f,440.f);exposure=ex;sunTime=st;sunIntensity=si;wind=wi;ambient=am;worldFog=fo;dirty=false;Notify("Scene loaded");
}
void AddAsset(int index,int parent=12){Remember();Node n;n.id=serial++;n.parent=parent;n.name=std::string(assets[index].name).substr(3)+"_"+std::to_string(n.id);n.icon=Icon::Cube;n.mesh=index;nodes.push_back(n);selected=n.id;int p=parent;for(int guard=0;p>=0&&guard<100;++guard){auto it=std::find_if(nodes.begin(),nodes.end(),[p](const Node& q){return q.id==p;});if(it==nodes.end())break;it->open=true;p=it->parent;}Notify("Added "+n.name+" to scene");}
void Duplicate(){Node* n=Current();if(!n||n->folder)return;Node copy=*n;Remember();copy.id=serial++;copy.name+="_Copy";copy.position[0]+=.5f;nodes.push_back(copy);selected=copy.id;Notify("Actor duplicated");}
void Delete(){Node* n=Current();if(!n||n->folder||n->locked)return;Remember();int id=selected;nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[id](const Node& q){return q.id==id;}),nodes.end());selected=10;if(!Current())selected=nodes.front().id;Notify("Actor removed — Ctrl+Z to undo");}
constexpr float fontFactor=1.13f;
void Txt(float x,float y,const std::string& text,ImU32 color=TEXT,float size=10){dl->AddText(regular,size*s*fontFactor,{x,y},color,text.c_str());}
void CenterText(Rect r,const std::string& text,ImU32 c=TEXT,float fs=10){ImVec2 z=regular->CalcTextSizeA(fs*s*fontFactor,10000,0,text.c_str());Txt(r.x+(r.w-z.x)/2,r.y+(r.h-z.y)/2,text,c,fs);}
void Box(Rect r,ImU32 c,float rounding=3,bool border=false){dl->AddRectFilled(r.Min(),r.Max(),c,rounding*s);if(border)dl->AddRect(r.Min(),r.Max(),BORDER,rounding*s);}
void IconAt(Icon i,float x,float y,ImU32 c=MUTED,float z=11){DrawIcon(dl,i,{x,y},z*s,c);}
void Tip(const char* t){if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",t);}
bool Hit(const char* id,Rect r){ImGui::SetCursorScreenPos(r.Min());return ImGui::InvisibleButton(id,{std::max(1.f,r.w),std::max(1.f,r.h)});}
bool Button(const char* id,Rect r,const char* label,Icon i=Icon::More,bool icon=true,bool active=false,bool arrow=false,ImU32 bg=RAISED){
 bool clicked=Hit(id,r);bool hover=ImGui::IsItemHovered();Box(r,active?SELECT:(hover?IM_COL32(51,60,68,255):bg),3,true);
 if(icon)IconAt(i,r.x+6*s,r.y+(r.h-11*s)/2,active?CYAN:TEXT);
 if(label&&*label)Txt(r.x+(icon?22:7)*s,r.y+(r.h-10*s)/2,label,TEXT);
 if(arrow)IconAt(Icon::ChevronDown,r.x+r.w-13*s,r.y+(r.h-9*s)/2,MUTED,9);
 return clicked;
}
bool IconButton(const char* id,Rect r,Icon i,const char* tip,bool active=false){bool q=Hit(id,r);if(ImGui::IsItemHovered()||active)Box(r,active?SELECT:RAISED,3);IconAt(i,r.x+(r.w-12*s)/2,r.y+(r.h-12*s)/2,active?CYAN:TEXT,12);Tip(tip);return q;}
bool Tab(const char* id,Rect r,const char* label,Icon icon,bool active){bool q=Hit(id,r);if(active)Box(r,RAISED,3,true);else if(ImGui::IsItemHovered())Box(r,RAISED);IconAt(icon,r.x+8*s,r.y+(r.h-11*s)/2,active?TEXT:MUTED);Txt(r.x+24*s,r.y+(r.h-10*s)/2,label,active?TEXT:MUTED);return q;}
void Image(Rect r,float x,float y,float w,float h,ImU32 tint=IM_COL32_WHITE){if(!ref.id)return;dl->AddImage((ImTextureID)(intptr_t)ref.id,r.Min(),r.Max(),{x/ref.width,y/ref.height},{(x+w)/ref.width,(y+h)/ref.height},tint);}
bool Input(const char* id,Rect r,char* value,size_t length,const char* hint="",bool search=false){
 ImGui::SetCursorScreenPos(r.Min());ImGui::SetNextItemWidth(r.w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{search?24*s:6*s,(r.h-11*s)/2});bool q=ImGui::InputTextWithHint(("##"+std::string(id)).c_str(),hint,value,length);ImGui::PopStyleVar();if(search)IconAt(Icon::Search,r.x+8*s,r.y+(r.h-10*s)/2,MUTED,10);return q;
}
bool Check(const char* id,float x,float y,bool& v){ImGui::SetCursorScreenPos({x,y});ImGui::PushStyleColor(ImGuiCol_FrameBg,ImColor(v?BLUE:BG).Value);bool q=ImGui::Checkbox(("##"+std::string(id)).c_str(),&v);ImGui::PopStyleColor();return q;}
bool Header(const char* id,Rect r,const char* label,int index){bool q=Hit(id,r);Box(r,RAISED,2);IconAt(expanded[index]?Icon::ChevronDown:Icon::ChevronRight,r.x+5*s,r.y+6*s,TEXT,9);Txt(r.x+17*s,r.y+5*s,label,TEXT);IconAt(expanded[index]?Icon::ChevronDown:Icon::ChevronRight,r.x+r.w-13*s,r.y+6*s,MUTED,9);if(q)expanded[index]=!expanded[index];return expanded[index];}
bool Combo(const char* id,Rect r,int& value,const char* const* options,int count){const char* label=options[std::clamp(value,0,count-1)];bool q=false;if(Button(id,r,label,Icon::More,false,false,true))ImGui::OpenPopup(id);if(ImGui::BeginPopup(id)){for(int i=0;i<count;++i)if(ImGui::Selectable(options[i],value==i)){value=i;q=true;}ImGui::EndPopup();}return q;}
void Thumb(Rect r,int index){const Asset& a=assets[std::clamp(index,0,15)];Image(r,a.x,a.y,a.w,a.h);}
void TopBar(float width){
 Box({0,0,width,35*s},IM_COL32(21,26,30,255),0);dl->AddLine({0,35*s},{width,35*s},BORDER);
 IconAt(Icon::Cube,15*s,10*s,CYAN,16);dl->AddLine({43*s,7*s},{43*s,29*s},BORDER);
 const char* menus[]={"Project","Edit","View","Scene","Tools","Build","Help"};float x=52*s;
 for(int m=0;m<7;++m){std::string id="menu"+std::to_string(m);float w=(m==0?51:43)*s;if(Hit(id.c_str(),{x,5*s,w,25*s}))ImGui::OpenPopup(id.c_str());if(ImGui::IsItemHovered())Box({x,5*s,w,25*s},RAISED);Txt(x+7*s,12*s,menus[m]);
  if(ImGui::BeginPopup(id.c_str())){
   if(m==0){if(ImGui::MenuItem("New scene","Ctrl+N"))wantReset=true;if(ImGui::MenuItem("Open scene","Ctrl+O"))Load();if(ImGui::MenuItem("Save scene","Ctrl+S"))Save();ImGui::Separator();ImGui::TextDisabled("%s",std::filesystem::absolute(statePath).string().c_str());}
   if(m==1){if(ImGui::MenuItem("Undo","Ctrl+Z",false,!past.empty()))Undo();if(ImGui::MenuItem("Redo","Ctrl+Y",false,!future.empty()))Redo();ImGui::Separator();if(ImGui::MenuItem("Duplicate actor","Ctrl+D"))Duplicate();if(ImGui::MenuItem("Delete actor","Delete"))Delete();}
   if(m==2){ImGui::MenuItem("Viewport statistics",nullptr,&stats);ImGui::MenuItem("Grid",nullptr,&grid);ImGui::MenuItem("Focus viewport","F11",&focusView);if(ImGui::MenuItem("Reset layout")){leftWidth=198;rightWidth=270;bottomHeight=253;}if(ImGui::MenuItem("Reset camera","F")){zoom=1;panX=panY=0;}}
   if(m==3){if(ImGui::MenuItem("Add selected asset"))AddAsset(selectedAsset);if(ImGui::MenuItem("Duplicate selection"))Duplicate();if(ImGui::MenuItem("World settings"))inspectorTab=2;}
   if(m==4){if(ImGui::MenuItem("Move","W"))tool=0;if(ImGui::MenuItem("Rotate","E"))tool=1;if(ImGui::MenuItem("Scale","R"))tool=2;ImGui::Separator();ImGui::MenuItem("Snapping",nullptr,&snap);if(ImGui::MenuItem("Lighting"))inspectorTab=1;}
   if(m==5){if(ImGui::MenuItem("Validate scene")){int invalid=0;for(auto& n:nodes)if(!n.folder)for(float f:n.scale)if(f==0)++invalid;Notify(invalid?"Validation: zero scale detected":"Validation passed — scene data is consistent");}if(ImGui::MenuItem("Save snapshot"))Save();}
   if(m==6){if(ImGui::MenuItem("Controls & shortcuts"))wantHelp=true;if(ImGui::MenuItem("About Atelier"))wantAbout=true;}
   ImGui::EndPopup();}x+=w;
 }
 x=std::max(448*s,width*.35f);
 if(IconButton("play",{x,7*s,29*s,23*s},playing?Icon::Pause:Icon::Play,playing?"Pause preview (Space)":"Play preview (Space)",playing))playing=!playing;
 if(IconButton("step",{x+31*s,7*s,25*s,23*s},Icon::Step,"Advance preview by one frame")){animationTime=std::min(animationDuration,animationTime+1/30.f);Notify("Preview advanced one frame");}
 Icon tools[]={Icon::Move,Icon::Rotate,Icon::Scale};float offsets[]={67,207,235};for(int i=0;i<3;++i)if(IconButton(("tool"+std::to_string(i)).c_str(),{x+offsets[i]*s,7*s,25*s,23*s},tools[i],i==0?"Move (W)":i==1?"Rotate (E)":"Scale (R)",tool==i))tool=i;
 const char* spaces[]={"Local","World"};Combo("coordinateSpace",{x+94*s,7*s,72*s,23*s},space,spaces,2);
 if(IconButton("snapping",{x+175*s,7*s,25*s,23*s},Icon::Magnet,"Toggle snapping (0.5 units / 15 degrees)",snap))snap=!snap;
 if(IconButton("cameraTop",{x+266*s,7*s,25*s,23*s},Icon::Camera,"Focus selection (F)")){zoom=1;panX=panY=0;}
 if(IconButton("gridTop",{x+296*s,7*s,25*s,23*s},Icon::Grid,"Show viewport grid",grid))grid=!grid;
 if(Button("speed",{x+327*s,7*s,31*s,23*s},(std::to_string(cameraSpeed).substr(0,3)).c_str(),Icon::More,false,false,true))ImGui::OpenPopup("cameraSpeed");if(ImGui::BeginPopup("cameraSpeed")){ImGui::SliderFloat("Camera speed",&cameraSpeed,.1f,5,"%.1f");ImGui::EndPopup();}
 if(IconButton("link",{x+365*s,7*s,24*s,23*s},Icon::Link,"Reference image and native UI"))wantAbout=true;
 const char* levels[]={"Dev Level","Preview","Production"};static int level=0;Combo("level",{width-172*s,7*s,77*s,23*s},level,levels,3);
 if(IconButton("lightingTop",{width-88*s,7*s,24*s,23*s},Icon::Sun,"Lighting settings",inspectorTab==1))inspectorTab=1;
 if(IconButton("display",{width-61*s,7*s,24*s,23*s},Icon::Monitor,"Toggle performance overlay",stats))stats=!stats;
 if(IconButton("profile",{width-31*s,7*s,23*s,23*s},Icon::Person,"About Atelier"))wantAbout=true;
 if(Button("openGraphWorkspace",{width-300*s,7*s,117*s,23*s},"Graph Editor",Icon::Blueprint))activeWorkspace=1;
 if(Button("openMaterialWorkspace",{width-440*s,7*s,128*s,23*s},"Material Studio",Icon::Material))activeWorkspace=2;
}
bool AncestorVisible(const Node& node){int p=node.parent;int guard=0;while(p>=0&&guard++<100){auto it=std::find_if(nodes.begin(),nodes.end(),[p](const Node& q){return q.id==p;});if(it==nodes.end())return true;if(!it->open)return false;p=it->parent;}return true;}
int Depth(const Node& node){int d=0,p=node.parent;while(p>=0&&d<10){++d;auto it=std::find_if(nodes.begin(),nodes.end(),[p](const Node& q){return q.id==p;});if(it==nodes.end())break;p=it->parent;}return d;}
void Hierarchy(Rect r){
 Box(r,PANEL,4,true);if(Tab("SceneTab",{r.x+4*s,r.y,59*s,24*s},"Scene",Icon::Scene,sceneTab==0))sceneTab=0;if(Tab("WorldTab",{r.x+65*s,r.y,60*s,24*s},"World",Icon::World,sceneTab==1))sceneTab=1;
 if(IconButton("addScene",{r.x+r.w-33*s,r.y,25*s,24*s},Icon::Plus,"Add actor"))ImGui::OpenPopup("newActor");if(ImGui::BeginPopup("newActor")){for(int i=0;i<16;++i)if(ImGui::MenuItem(assets[i].name))AddAsset(i);ImGui::EndPopup();}
 Input("sceneSearch",{r.x+7*s,r.y+27*s,r.w-14*s,23*s},sceneSearch,sizeof(sceneSearch),"Search scene...",true);
 float top=r.y+55*s;ImGui::SetCursorScreenPos({r.x+3*s,top});ImGui::BeginChild("sceneTree",{r.w-6*s,r.h-59*s},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);dl=ImGui::GetWindowDrawList();float y=ImGui::GetCursorScreenPos().y;
 if(sceneTab==1){
  Txt(r.x+14*s,y,"WORLD OVERVIEW",MUTED,9);y+=25*s;
  for(auto item:std::array<std::pair<const char*,int>,4>{{{"Actors",(int)nodes.size()},{"Static meshes",(int)std::count_if(nodes.begin(),nodes.end(),[](const Node& n){return !n.folder;})},{"Folders",(int)std::count_if(nodes.begin(),nodes.end(),[](const Node& n){return n.folder;})},{"Assets",16}}}){Txt(r.x+14*s,y,item.first,MUTED);Txt(r.x+r.w-43*s,y,std::to_string(item.second));y+=23*s;}
  y+=12*s;if(Button("worldSettings",{r.x+12*s,y,r.w-24*s,25*s},"World Settings",Icon::World))inspectorTab=2;y+=37*s;
  Txt(r.x+14*s,y,"SCENE COLLECTIONS",MUTED,9);y+=23*s;
 }
 int deleteAfter=-1,duplicateAfter=-1,reparentActor=-1,reparentFolder=-1;std::vector<size_t> order;std::set<int> visited;
 std::function<void(int)> walk=[&](int parent){for(size_t i=0;i<nodes.size();++i)if(nodes[i].parent==parent&&visited.insert(nodes[i].id).second){order.push_back(i);walk(nodes[i].id);}};walk(-1);
 for(size_t index:order){auto& n=nodes[index];if(!Matches(n.name,sceneSearch))continue;if(!*sceneSearch&&!AncestorVisible(n))continue;if(sceneTab==1&&!n.folder)continue;int depth=Depth(n);float indent=7.f+depth*13.f;Rect row={r.x+3*s,y,r.w-6*s,17*s};ImGui::PushID(n.id);
  bool q=Hit("node",{row.x,row.y,row.w-44*s,row.h});if(selected==n.id)Box(row,SELECT,2);else if(ImGui::IsItemHovered())Box(row,RAISED,2);
  if(q){selected=n.id;if(n.folder)n.open=!n.open;else selectedAsset=n.mesh;}
  if(ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("ACTOR",&n.id,sizeof(n.id));ImGui::Text("%s",n.name.c_str());ImGui::EndDragDropSource();}
  if(n.folder&&ImGui::BeginDragDropTarget()){if(const ImGuiPayload* p=ImGui::AcceptDragDropPayload("ASSET")){int a=*(int*)p->Data,parent=n.id;AddAsset(a,parent);ImGui::EndDragDropTarget();ImGui::PopID();break;}if(const ImGuiPayload* p=ImGui::AcceptDragDropPayload("ACTOR")){reparentActor=*(int*)p->Data;reparentFolder=n.id;}ImGui::EndDragDropTarget();}
  if(ImGui::BeginPopupContextItem("nodeContext")){selected=n.id;if(ImGui::MenuItem("Rename","F2")){renaming=n.id;std::snprintf(renameText,sizeof(renameText),"%s",n.name.c_str());}if(ImGui::MenuItem("Duplicate","Ctrl+D",false,!n.folder))duplicateAfter=n.id;if(ImGui::MenuItem("Delete","Delete",false,!n.folder&&!n.locked))deleteAfter=n.id;ImGui::EndPopup();}
  if(n.folder)IconAt(n.open?Icon::ChevronDown:Icon::ChevronRight,r.x+indent*s,y+4*s,TEXT,8);
  IconAt(n.folder?Icon::Folder:n.icon,r.x+(indent+13)*s,y+3*s,n.visible?TEXT:MUTED,10);Txt(r.x+(indent+27)*s,y+3*s,n.name,n.visible?TEXT:MUTED,9.5f);
  if(IconButton("visible",{r.x+r.w-43*s,y,19*s,17*s},Icon::Eye,n.visible?"Hide actor":"Show actor",!n.visible)){Remember();n.visible=!n.visible;}
  if(IconButton("locked",{r.x+r.w-24*s,y,18*s,17*s},Icon::Lock,n.locked?"Unlock actor":"Lock actor",n.locked)){Remember();n.locked=!n.locked;}
  ImGui::PopID();y+=17*s;
 }
 ImGui::SetCursorScreenPos({r.x+4*s,y});ImGui::Dummy({1,1});ImGui::EndChild();dl=ImGui::GetWindowDrawList();
 if(deleteAfter>=0){selected=deleteAfter;Delete();}if(duplicateAfter>=0){selected=duplicateAfter;Duplicate();}
 if(reparentActor>=0){bool cycle=false;int p=reparentFolder;for(int guard=0;p>=0&&guard<100;++guard){if(p==reparentActor){cycle=true;break;}auto it=std::find_if(nodes.begin(),nodes.end(),[p](const Node& q){return q.id==p;});if(it==nodes.end())break;p=it->parent;}if(!cycle){for(auto& n:nodes)if(n.id==reparentActor&&!n.locked){Remember();n.parent=reparentFolder;Notify("Actor moved to folder");break;}}}
}
void Viewport(Rect r){
 Box(r,BG,4,true);dl->PushClipRect(r.Min(),r.Max(),true);
 // Subrects sample the supplied concept; surrounding interface is entirely native.
 float uw=786.f/zoom,uh=412.f/zoom,cx=605+panX,cy=247+panY;
 float x=std::clamp(cx-uw*.5f,211.f,997.f-uw),y=std::clamp(cy-uh*.5f,41.f,453.f-uh);
 float brightness=std::clamp(std::pow(2.f,exposure)*((sunTime<8||sunTime>20)?.6f:1.f),.18f,1.f);
 if(sceneTexture.id)dl->AddImage((ImTextureID)(intptr_t)sceneTexture.id,r.Min(),r.Max(),{x/ref.width,y/ref.height},{(x+uw)/ref.width,(y+uh)/ref.height},IM_COL32((int)(255*brightness),(int)(255*brightness),(int)(255*brightness),255));else Image(r,x,y,uw,uh,IM_COL32((int)(255*brightness),(int)(255*brightness),(int)(255*brightness),255));
 ImGui::SetNextItemAllowOverlap();bool clicked=Hit("viewportCanvas",r);Tip("Reference viewport — wheel to zoom, right drag to pan; drop an asset to add it");
 if(ImGui::IsItemHovered()){
  float wheel=ImGui::GetIO().MouseWheel;if(wheel!=0)zoom=std::clamp(zoom+wheel*.1f,1.f,3.f);
  if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){panX-=ImGui::GetIO().MouseDelta.x/zoom*cameraSpeed;panY-=ImGui::GetIO().MouseDelta.y/zoom*cameraSpeed;}
  if(clicked){selected=10;selectedAsset=0;}
 }
 if(ImGui::BeginDragDropTarget()){if(const ImGuiPayload* p=ImGui::AcceptDragDropPayload("ASSET"))AddAsset(*(int*)p->Data);ImGui::EndDragDropTarget();}
 if(grid){for(int i=1;i<15;++i){float xx=r.x+r.w*i/15;dl->AddLine({xx,r.y+r.h*.58f},{r.x+r.w/2+(xx-r.x-r.w/2)*1.5f,r.y+r.h},IM_COL32(149,176,187,75));}for(int i=0;i<9;++i){float yy=r.y+r.h*(.6f+i*.045f);dl->AddLine({r.x,yy},{r.x+r.w,yy},IM_COL32(149,176,187,70));}}
 Node* n=Current();if(n&&!n->folder&&n->visible){float gx=r.x+r.w*.46f,gy=r.y+r.h*.51f;
  if(sceneTexture.id||selected!=10||zoom>1.01f){float xx=gx+(n->position[0]-12.843f)*8*s,yy=gy-(n->position[1]-5.129f)*8*s;dl->AddRect({xx-3*s,yy-18*s},{xx+16*s,yy},CYAN,0,0,1.2f*s);dl->AddLine({xx,yy},{xx+45*s,yy},IM_COL32(226,65,60,255),2*s);dl->AddTriangleFilled({xx+45*s,yy},{xx+38*s,yy-4*s},{xx+38*s,yy+4*s},IM_COL32(226,65,60,255));dl->AddLine({xx,yy},{xx,yy-55*s},IM_COL32(75,153,237,255),2*s);dl->AddTriangleFilled({xx,yy-55*s},{xx-4*s,yy-47*s},{xx+4*s,yy-47*s},IM_COL32(75,153,237,255));dl->AddLine({xx,yy},{xx-30*s,yy},IM_COL32(129,208,50,255),2*s);}
  // Dragging the gizmo edits the same actor data as the Transform fields.
  if(Hit("gizmoHandle",{gx-18*s,gy-58*s,76*s,81*s})){}Tip("Drag to change the selected actor transform");
  static bool gizmoChanged=false;if(ImGui::IsItemActivated()){editBefore=Capture();gizmoChanged=false;}
  if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)&&!n->locked){auto delta=ImGui::GetIO().MouseDelta;if(tool==0){n->position[0]+=delta.x*.025f;n->position[1]-=delta.y*.025f;}if(tool==1)n->rotation[1]+=delta.x;if(tool==2){n->scale[0]=std::max(.01f,n->scale[0]+delta.x*.01f);n->scale[1]=n->scale[2]=n->scale[0];}dirty=true;gizmoChanged=true;}
  if(ImGui::IsItemDeactivated()&&gizmoChanged){if(snap){if(tool==0)for(auto& v:n->position)v=std::round(v*2)/2;if(tool==1)for(auto& v:n->rotation)v=std::round(v/15)*15;}past.push_back(editBefore);future.clear();gizmoChanged=false;}
 }
 // Cover the concept's baked toolbar with matching working controls.
 float tx=r.x+7*s,ty=r.y+7*s;
 Box({tx,ty,22*s,21*s},IM_COL32(51,58,65,220));if(IconButton("viewportSettings",{tx,ty,22*s,21*s},Icon::Settings,"Viewport options"))ImGui::OpenPopup("viewportOptions");if(ImGui::BeginPopup("viewportOptions")){ImGui::Checkbox("Statistics",&stats);ImGui::Checkbox("Grid",&grid);if(ImGui::Button("Reset view")){zoom=1;panX=panY=0;}ImGui::EndPopup();}
 const char* views[]={"Perspective","Top","Front","Right"};Combo("view",{tx+28*s,ty,74*s,21*s},perspective,views,4);
 const char* modes[]={"Lit","Unlit","Wireframe"};Combo("shading",{tx+108*s,ty,47*s,21*s},viewMode,modes,3);
 if(Button("show",{tx+161*s,ty,45*s,21*s},"Show",Icon::More,false,false,true))ImGui::OpenPopup("showOptions");if(ImGui::BeginPopup("showOptions")){ImGui::Checkbox("Statistics",&stats);ImGui::Checkbox("Grid",&grid);ImGui::Checkbox("Ambient light",&ambient);ImGui::Checkbox("World fog",&worldFog);ImGui::EndPopup();}
 if(IconButton("realTime",{tx+212*s,ty,21*s,21*s},Icon::Camera,"Focus selection (F)")){zoom=1;panX=panY=0;}
 if(IconButton("viewInfo",{tx+234*s,ty,18*s,21*s},Icon::Info,"Preview information"))wantAbout=true;
 if(viewMode==2){for(int i=0;i<22;++i){float xx=r.x+r.w*i/22;dl->AddLine({xx,r.y+32*s},{r.x+r.w- (xx-r.x),r.y+r.h},IM_COL32(135,216,229,65));}Txt(r.x+11*s,r.y+r.h-20*s,"Wireframe overlay · reference preview",CYAN,9);}
 if(viewMode==1)dl->AddRectFilled({r.x,r.y+32*s},r.Max(),IM_COL32(180,195,214,22));
 if(perspective!=0){Box({r.x+r.w/2-59*s,r.y+9*s,118*s,20*s},IM_COL32(23,28,32,210));CenterText({r.x+r.w/2-59*s,r.y+9*s,118*s,20*s},std::string(views[perspective])+" · image preview",MUTED,9);}
 if(stats){Rect st={r.x+r.w-122*s,r.y+7*s,108*s,94*s};Box(st,IM_COL32(60,65,72,220),4);const char* names[]={"FPS","Frame","Triangles","Draw Calls","Objects","Lights"};char fps[30],frame[30];std::snprintf(fps,sizeof(fps),"%.0f",ImGui::GetIO().Framerate);std::snprintf(frame,sizeof(frame),"%.1f ms",1000.f/std::max(1.f,ImGui::GetIO().Framerate));const char* values[]={fps,frame,"1.4 M","642","318","12"};for(int i=0;i<6;++i){Txt(st.x+8*s,st.y+(8+i*14)*s,names[i],TEXT,9);ImVec2 z=regular->CalcTextSizeA(9*s,1000,0,values[i]);Txt(st.x+st.w-9*s-z.x,st.y+(8+i*14)*s,values[i],TEXT,9);}Hit("statsTooltip",st);Tip("FPS and Frame are measured. Geometry figures reproduce the concept scene.");}
 ImVec2 origin={r.x+33*s,r.y+r.h-27*s};ImU32 red=IM_COL32(224,70,62,255),green=IM_COL32(130,194,82,255),cyan=IM_COL32(59,175,220,255);dl->AddLine(origin,{origin.x+22*s,origin.y},red,1.5f*s);dl->AddLine(origin,{origin.x,origin.y-22*s},cyan,1.5f*s);dl->AddCircleFilled(origin,2*s,TEXT);Txt(origin.x+24*s,origin.y-4*s,"X",red,8);Txt(origin.x-3*s,origin.y-30*s,"Z",cyan,8);Txt(origin.x-12*s,origin.y-4*s,"Y",green,8);if(Hit("orientationAxes",{origin.x-13*s,origin.y-30*s,45*s,40*s}))perspective=(perspective+1)%4;Tip("Cycle preview orientation");
 if(playing){Box({r.x+r.w/2-48*s,r.y+r.h-27*s,96*s,20*s},IM_COL32(27,43,48,235));char t[40];std::snprintf(t,sizeof(t),"PLAY  %.2f s",animationTime);CenterText({r.x+r.w/2-48*s,r.y+r.h-27*s,96*s,20*s},t,CYAN,9);}
 dl->PopClipRect();dl->AddRect(r.Min(),r.Max(),BORDER,3*s);
}
void ContentFolders(Rect r){
 dl->AddLine({r.x+r.w,r.y},{r.x+r.w,r.y+r.h},BORDER);float y=r.y+3*s;
 if(Hit("favorites",{r.x+3*s,y,r.w-6*s,17*s})){favorites=!favorites;}if(favorites)Box({r.x+3*s,y,r.w-6*s,17*s},SELECT);IconAt(Icon::Sun,r.x+8*s,y+4*s,TEXT,9);Txt(r.x+23*s,y+3*s,"Favorites",TEXT,9);y+=18*s;
 if(Hit("contentRoot",{r.x+3*s,y,r.w-6*s,17*s})){folder=-1;favorites=false;}IconAt(Icon::ChevronDown,r.x+5*s,y+4*s,TEXT,8);IconAt(Icon::Folder,r.x+15*s,y+3*s,TEXT,10);Txt(r.x+29*s,y+3*s,"Content",TEXT,9);y+=16*s;
 if(Hit("envFolder",{r.x+3*s,y,r.w-6*s,17*s})){folder=-1;favorites=false;}if(folder==-1&&!favorites)Box({r.x+3*s,y,r.w-6*s,17*s},SELECT,2);IconAt(Icon::ChevronDown,r.x+13*s,y+4*s,TEXT,8);IconAt(Icon::Folder,r.x+23*s,y+3*s,TEXT,10);Txt(r.x+37*s,y+3*s,"Environment",TEXT,9);y+=16*s;
 const char* names[]={"Ruins","Nature","Props","Characters","Gameplay","Materials","Animations","Blueprints","Audio","VFX"};
 for(int i=0;i<10;++i){float indent=i<3?26.f:12.f;if(Hit(("folder"+std::to_string(i)).c_str(),{r.x+3*s,y,r.w-6*s,14.5f*s})){folder=i;favorites=false;if(i==5)contentTab=1;else if(i==6)contentTab=2;else if(i==7)contentTab=3;else contentTab=0;}if(folder==i)Box({r.x+3*s,y,r.w-6*s,14.5f*s},SELECT,2);IconAt(Icon::ChevronRight,r.x+indent*s,y+3*s,MUTED,7);IconAt(Icon::Folder,r.x+(indent+10)*s,y+3*s,TEXT,9);Txt(r.x+(indent+23)*s,y+2*s,names[i],TEXT,8.6f);y+=14.5f*s;}
}
void AssetDetails(Rect r){
 dl->AddLine(r.Min(),{r.x,r.y+r.h},BORDER);const Asset& a=assets[selectedAsset];IconAt(Icon::Cube,r.x+10*s,r.y+8*s,MUTED,13);Txt(r.x+28*s,r.y+7*s,a.name,TEXT,9);Txt(r.x+28*s,r.y+20*s,a.type,MUTED,8);
 Rect thumb={r.x+8*s,r.y+33*s,r.w-16*s,83*s};Box(thumb,BG,3,true);if(selectedAsset==0)Image({thumb.x+21*s,thumb.y+4*s,thumb.w-42*s,thumb.h-7*s},864,550,95,76);else Thumb({thumb.x+(thumb.w-68*s)/2,thumb.y+5*s,68*s,73*s},selectedAsset);
 if(Hit("assetLargePreview",thumb))wantAsset=true;Tip("Open asset preview");
 float y=r.y+124*s;const char* labels[]={"Triangles","Vertices","LODs","Size"};std::string values[]={std::to_string(a.triangles),std::to_string(a.vertices),"3","4.2 × 3.8 × 1.1 m"};for(int i=0;i<4;++i){Txt(r.x+10*s,y,labels[i],MUTED,8.5f);Txt(r.x+60*s,y,values[i],TEXT,8.5f);y+=13*s;}
 const char* tags[]={"ruins","stone","architecture"};float x=r.x+10*s;for(auto tag:tags){float w=regular->CalcTextSizeA(8*s,1000,0,tag).x+9*s;Box({x,y+3*s,w,15*s},RAISED,5);CenterText({x,y+3*s,w,15*s},tag,MUTED,8);x+=w+3*s;}
}
void AssetGrid(Rect r){
 ImGui::SetCursorScreenPos(r.Min());ImGui::BeginChild("assetGrid",{r.w,r.h},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);dl=ImGui::GetWindowDrawList();float y=ImGui::GetCursorScreenPos().y+8*s;float thumb=thumbnail*s,gap=6*s;int cols=std::max(1,(int)((r.w-14*s+gap)/(thumb+gap))),shown=0;float x=r.x+11*s;
 for(int i=0;i<16;++i){const auto& a=assets[i];if(!Matches(a.name,assetSearch))continue;if(favorites&&i!=0&&i!=1&&i!=15)continue;if(folder>=0&&folder<3&&a.folder!=std::string(folder==0?"Ruins":folder==1?"Nature":"Props"))continue;if(folder>=3&&folder!=5&&folder!=6&&folder!=7)continue;if(assetFilter==1&&std::string(a.folder)!="Ruins")continue;if(assetFilter==2&&std::string(a.folder)!="Nature")continue;
  ImGui::PushID(i);Rect card=listMode?Rect{x,y,r.w-22*s,38*s}:Rect{x,y,thumb,thumb+22*s};bool q=Hit("asset",card);bool hovered=ImGui::IsItemHovered();if(q)selectedAsset=i;if(hovered&&ImGui::IsMouseDoubleClicked(0)){selectedAsset=i;wantAsset=true;}
  Rect tile=listMode?Rect{x,y,34*s,34*s}:Rect{x,y,thumb,thumb+5*s};Box(tile,RAISED,3,true);if(selectedAsset==i)dl->AddRect(tile.Min(),tile.Max(),CYAN,3*s,0,1.5f*s);else if(hovered)dl->AddRect(tile.Min(),tile.Max(),MUTED,3*s);
  Thumb({tile.x+4*s,tile.y+4*s,tile.w-8*s,tile.h-8*s},i);
  if(listMode){Txt(x+44*s,y+5*s,a.name,TEXT,10);Txt(x+44*s,y+20*s,a.folder,MUTED,8);}else CenterText({x-4*s,y+thumb+7*s,thumb+8*s,14*s},a.name,TEXT,7.5f);
  Tip("Click to select · double click to inspect · drag to the viewport");if(ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("ASSET",&i,sizeof(i));ImGui::Text("%s",a.name);Thumb({ImGui::GetCursorScreenPos().x,ImGui::GetCursorScreenPos().y,60*s,60*s},i);ImGui::Dummy({60*s,60*s});ImGui::EndDragDropSource();}
  if(ImGui::BeginPopupContextItem("assetActions")){selectedAsset=i;if(ImGui::MenuItem("Open preview"))wantAsset=true;if(ImGui::MenuItem("Add to scene"))AddAsset(i);if(ImGui::MenuItem("Assign to selected actor")){if(Node* n=Current();n&&!n->locked){Remember();n->mesh=i;Notify("Mesh assigned");}}ImGui::EndPopup();}
  ImGui::PopID();++shown;if(listMode)y+=43*s;else if(shown%cols==0){x=r.x+11*s;y+=thumb+29*s;}else x+=thumb+gap;
 }
 if(!shown){CenterText({r.x,r.y+24*s,r.w,22*s},"No matching assets",MUTED);if(Button("clearFilters",{r.x+20*s,r.y+56*s,r.w-40*s,24*s},"Clear filters",Icon::Refresh)){folder=-1;favorites=false;assetFilter=0;assetSearch[0]=0;}}
 ImGui::SetCursorScreenPos({r.x+2*s,y+thumb+25*s});ImGui::Dummy({1,1});ImGui::EndChild();dl=ImGui::GetWindowDrawList();
}
void Materials(Rect r){
 const char* names[]={"M_Stone_Ruins","M_Stone_Detail","M_Stone_Warm"};float x=r.x+18*s;
 for(int i=0;i<3;++i){Rect card={x,r.y+14*s,108*s,136*s};if(Hit(("materialCard"+std::to_string(i)).c_str(),card)){pickerTarget=i;Notify(std::string(names[i])+" selected");}Box(card,RAISED,4,true);if(i==pickerTarget)dl->AddRect(card.Min(),card.Max(),CYAN,4*s);Image({x+24*s,r.y+23*s,60*s,60*s},1099,i==1?402.f:365.f,30,31);CenterText({x,r.y+92*s,108*s,17*s},names[i],TEXT,9);if(Button(("assignMat"+std::to_string(i)).c_str(),{x+8*s,r.y+119*s,92*s,23*s},"Assign",Icon::Material)){if(Node* n=Current();n&&!n->locked){Remember();n->material[0]=i;Notify("Material assigned to "+n->name);}}x+=121*s;}
}
void Animation(Rect r){
 float x=r.x+16*s,y=r.y+12*s;Txt(x,y,"Ancient Coast · Preview sequence",TEXT,11);y+=28*s;
 if(IconButton("animPlay",{x,y,28*s,24*s},playing?Icon::Pause:Icon::Play,"Play / pause sequence",playing))playing=!playing;
 if(IconButton("animRestart",{x+34*s,y,28*s,24*s},Icon::Refresh,"Restart sequence"))animationTime=0;
 ImGui::SetCursorScreenPos({x+78*s,y+4*s});ImGui::Checkbox("Loop",&animationLoop);ImGui::SetCursorScreenPos({x+184*s,y+2*s});ImGui::SetNextItemWidth(120*s);ImGui::DragFloat("Duration",&animationDuration,.1f,.1f,120,"%.1f s");y+=39*s;
 ImGui::SetCursorScreenPos({x,y});ImGui::SetNextItemWidth(r.w-35*s);ImGui::SliderFloat("##timeline",&animationTime,0,animationDuration,"%.2f s");y+=28*s;
 Rect track={x,y,r.w-35*s,46*s};Box(track,BG,2,true);Txt(track.x+10*s,track.y+9*s,"Archway · rotation",MUTED,9);for(int i=0;i<=10;++i){float xx=track.x+145*s+(track.w-163*s)*i/10;dl->AddLine({xx,track.y+5*s},{xx,track.y+track.h-5*s},BORDER);Txt(xx,track.y+26*s,std::to_string(i),MUTED,8);}float xx=track.x+145*s+(track.w-163*s)*(animationTime/animationDuration);dl->AddLine({xx,track.y},{xx,track.y+track.h},CYAN,2*s);
}
void Blueprint(Rect r){
 dl->PushClipRect(r.Min(),r.Max(),true);Box(r,IM_COL32(24,29,34,255));for(float x=r.x;x<r.x+r.w;x+=20*s)for(float y=r.y;y<r.y+r.h;y+=20*s)dl->AddCircleFilled({x,y},.8f*s,BORDER);
 if(graphConnected&&graph.size()>1){for(size_t i=1;i<graph.size();++i){auto a=graph[i-1].p,b=graph[i].p;ImVec2 from={r.x+(a.x+154)*s,r.y+(a.y+41)*s},to={r.x+b.x*s,r.y+(b.y+41)*s};dl->AddBezierCubic(from,{from.x+48*s,from.y},{to.x-48*s,to.y},to,CYAN,2*s);}}
 for(size_t i=0;i<graph.size();++i){auto& n=graph[i];Rect card={r.x+n.p.x*s,r.y+n.p.y*s,154*s,71*s};bool q=Hit(("graphNode"+std::to_string(i)).c_str(),{card.x,card.y,card.w,23*s});if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){n.p.x+=ImGui::GetIO().MouseDelta.x/s;n.p.y+=ImGui::GetIO().MouseDelta.y/s;n.p.x=std::clamp(n.p.x,0.f,std::max(0.f,r.w/s-154));n.p.y=std::clamp(n.p.y,0.f,std::max(0.f,r.h/s-71));}Box(card,RAISED,4,true);Box({card.x,card.y,card.w,23*s},n.kind==0?IM_COL32(73,52,62,255):SELECT,4);Txt(card.x+10*s,card.y+6*s,n.label,TEXT,10);dl->AddCircleFilled({card.x+6*s,card.y+42*s},3*s,CYAN);dl->AddCircleFilled({card.x+card.w-6*s,card.y+42*s},3*s,CYAN);Txt(card.x+17*s,card.y+37*s,n.kind==0?"Execute":n.kind==1?"Yaw + 15°":"Visible = true",MUTED,9);if(q)Notify("Drag node header to arrange graph");}
 if(Button("runGraph",{r.x+r.w-100*s,r.y+7*s,88*s,23*s},"Run graph",Icon::Play)){if(Node* n=Current();n&&!n->locked){Remember();for(auto& g:graph){if(!graphConnected)break;if(g.kind==1)n->rotation[1]+=15;if(g.kind==2)n->visible=true;}Notify("Graph executed on "+n->name);}}
 if(Hit("graphContext",{r.x,r.y+r.h-25*s,200*s,25*s})&&ImGui::IsMouseClicked(1)){}if(ImGui::BeginPopupContextItem("graphMenu")){if(ImGui::MenuItem("Add Rotate node"))graph.push_back({"Rotate Archway",{40+20.f*graph.size(),85},1});if(ImGui::MenuItem("Add Visibility node"))graph.push_back({"Set Visibility",{40+20.f*graph.size(),85},2});ImGui::MenuItem("Connect nodes",nullptr,&graphConnected);if(ImGui::MenuItem("Reset graph")){graph={{"On Begin Play",{25,38},0},{"Rotate Archway",{219,74},1},{"Set Visibility",{412,38},2}};}ImGui::EndPopup();}Txt(r.x+12*s,r.y+r.h-18*s,"Drag headers · right click here to add nodes",MUTED,8);dl->PopClipRect();
}
void Content(Rect r){
 Box(r,PANEL,4,true);float x=r.x+5*s;
 const char* names[]={"Content Browser","Materials","Animations","Visual Scripting"};Icon icons[]={Icon::Cube,Icon::Material,Icon::Animation,Icon::Blueprint};float widths[]={97,83,94,110};
 for(int i=0;i<4;++i){if(Tab(("contentTab"+std::to_string(i)).c_str(),{x,r.y, widths[i]*s,26*s},names[i],icons[i],contentTab==i))contentTab=i;x+=widths[i]*s;}
 if(IconButton("moreContent",{x+3*s,r.y,24*s,26*s},Icon::Plus,"Content tools"))ImGui::OpenPopup("contentTools");if(ImGui::BeginPopup("contentTools")){if(ImGui::MenuItem("Add selected asset to scene"))AddAsset(selectedAsset);if(ImGui::MenuItem("Asset preview"))wantAsset=true;if(ImGui::MenuItem("Reset filters")){assetSearch[0]=0;folder=-1;assetFilter=0;favorites=false;}ImGui::EndPopup();}
 Rect body={r.x+2*s,r.y+57*s,r.w-4*s,r.h-59*s};float y=r.y+31*s;dl->AddLine({r.x,r.y+27*s},{r.x+r.w,r.y+27*s},BORDER);
 if(contentTab==0){
  if(IconButton("backFolder",{r.x+6*s,y,19*s,21*s},Icon::ArrowLeft,"Parent folder")){folder=-1;favorites=false;}
  if(IconButton("forwardFolder",{r.x+27*s,y,19*s,21*s},Icon::ArrowRight,"Open Ruins"))folder=0;
  if(IconButton("refreshAssets",{r.x+54*s,y,21*s,21*s},Icon::Refresh,"Refresh asset list")){assetSearch[0]=0;Notify("16 reference assets ready");}
  bool compact=r.w<710*s;if(!compact){IconAt(Icon::Search,r.x+89*s,y+6*s,MUTED,10);Txt(r.x+107*s,y+6*s,"Content",TEXT,9);IconAt(Icon::ChevronRight,r.x+149*s,y+7*s,MUTED,8);Txt(r.x+164*s,y+6*s,"Environment",TEXT,9);IconAt(Icon::ChevronRight,r.x+232*s,y+7*s,MUTED,8);Txt(r.x+248*s,y+6*s,folder==1?"Nature":folder==2?"Props":"Ruins",TEXT,9);}else{Txt(r.x+89*s,y+6*s,"Environment",TEXT,9);}
  float controls=r.x+r.w-233*s;float searchX=r.x+(compact?203:385)*s,searchW=std::max(50*s,controls-searchX-8*s);Input("assetSearch",{searchX,y,searchW,21*s},assetSearch,sizeof(assetSearch),"Search assets...",true);
  if(IconButton("assetGridToggle",{controls,y,22*s,21*s},Icon::Grid,"Thumbnail view",!listMode))listMode=false;if(IconButton("assetListToggle",{controls+26*s,y,22*s,21*s},Icon::List,"List view",listMode))listMode=true;
  if(IconButton("assetFilter",{controls+53*s,y,23*s,21*s},Icon::Filter,"Filter assets",assetFilter!=0))ImGui::OpenPopup("assetFilters");if(ImGui::BeginPopup("assetFilters")){const char* fs[]={"All assets","Ruins","Nature"};for(int i=0;i<3;++i)if(ImGui::Selectable(fs[i],assetFilter==i))assetFilter=i;ImGui::EndPopup();}
  IconAt(Icon::Plus,controls+88*s,y+7*s,MUTED,8);ImGui::SetCursorScreenPos({controls+110*s,y+6*s});ImGui::SetNextItemWidth(70*s);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{0,0});ImGui::SliderFloat("##thumbnailSize",&thumbnail,44,90,"");ImGui::PopStyleVar();Tip("Asset thumbnail size");
  if(IconButton("assetSound",{controls+184*s,y,21*s,21*s},Icon::Volume,"Preview selected asset"))wantAsset=true;
  if(IconButton("assetSettings",{controls+208*s,y,21*s,21*s},Icon::Settings,"Asset browser options"))ImGui::OpenPopup("browserSettings");if(ImGui::BeginPopup("browserSettings")){ImGui::Checkbox("Favorites only",&favorites);ImGui::Checkbox("List view",&listMode);ImGui::EndPopup();}
  float tree=114*s,details=165*s;ContentFolders({body.x,body.y,tree,body.h});AssetGrid({body.x+tree,body.y,body.w-tree-details,body.h});AssetDetails({body.x+body.w-details,body.y,details,body.h});
 }else if(contentTab==1){Txt(r.x+16*s,y+6*s,"Content  >  Materials",MUTED,9);Materials(body);}else if(contentTab==2){Txt(r.x+16*s,y+6*s,"Content  >  Animations",MUTED,9);Animation(body);}else{Txt(r.x+16*s,y+6*s,"Content  >  Blueprints  >  BP_AncientCoast",MUTED,9);Blueprint(body);}
}
void AxisFields(const char* id,float x,float y,float width,float* values,bool isScale=false){
 ImGui::PushID(id);float gap=4*s,w=(width-gap*2)/3;ImU32 colors[]={IM_COL32(173,77,78,255),IM_COL32(95,157,100,255),IM_COL32(80,129,201,255)};const char* axes[]={"X","Y","Z"};for(int i=0;i<3;++i){Rect r={x+i*(w+gap),y,w,18*s};ImGui::PushID(i);ImGui::SetCursorScreenPos(r.Min());ImGui::SetNextItemWidth(w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{12*s,3*s});ImGui::DragFloat("##axis",&values[i],isScale?.01f:.02f,isScale?.001f:-100000.f,100000.f,isScale?"%.2f":"%.3f");ImGui::PopStyleVar();TrackEdit();dl->AddLine({r.x+1*s,r.y+3*s},{r.x+1*s,r.y+r.h-3*s},colors[i],1.5f*s);Txt(r.x+4*s,r.y+4*s,axes[i],colors[i],8);Tip("Drag to edit · Ctrl+click for numeric entry");ImGui::PopID();}ImGui::PopID();
}
bool ShowProperty(const char* name){return Matches(name,propertySearch);}
void PropertyBool(const char* id,const char* label,float x,float y,bool& value,float fieldX){Txt(x,y+3*s,label,MUTED,9);bool old=value;if(Check(id,fieldX,y,value)){bool nv=value;value=old;Remember();value=nv;}}
void InspectorActor(Rect r){
 Node* n=Current();if(!n){CenterText(r,"Select an actor",MUTED);return;}
 float x=r.x+8*s,w=r.w-16*s,y=r.y+8*s;
 Thumb({x+2*s,y+4*s,23*s,25*s},n->mesh);Txt(x+33*s,y+4*s,n->name,TEXT,10);Txt(x+33*s,y+21*s,n->folder?"Scene Folder":"Static Mesh Actor",MUTED,8.5f);
 const char* mobility[]={"Static","Stationary","Movable"};int old=n->mobility;ImGui::BeginDisabled(n->locked);if(Combo("mobility",{x+w-65*s,y+7*s,63*s,21*s},n->mobility,mobility,3)){int nv=n->mobility;n->mobility=old;Remember();n->mobility=nv;}ImGui::EndDisabled();
 y+=39*s;ImGui::BeginDisabled(n->locked);if(Button("addComponent",{x,y,w,25*s},"Add Component",Icon::Plus)){ImGui::OpenPopup("components");}if(ImGui::BeginPopup("components")){const char* cs[]={"Point Light","Audio Source","Camera","Script","Particle System"};for(auto c:cs)if(ImGui::MenuItem(c)){Remember();n->components.push_back(c);Notify(std::string(c)+" component added");}ImGui::EndPopup();}ImGui::EndDisabled();y+=31*s;
 Input("propertySearch",{x,y,w,23*s},propertySearch,sizeof(propertySearch),"Search properties...",true);y+=29*s;
 ImGui::SetCursorScreenPos({r.x+3*s,y});ImGui::BeginChild("properties",{r.w-6*s,r.y+r.h-y-5*s},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);dl=ImGui::GetWindowDrawList();y=ImGui::GetCursorScreenPos().y;float labelX=x+7*s,fieldX=x+84*s,fieldW=w-89*s;
 if(n->locked){Box({x,y,w,23*s},SELECT);Txt(x+8*s,y+6*s,"Actor locked — unlock in Scene",CYAN,9);y+=29*s;ImGui::BeginDisabled();}
 if(ShowProperty("Transform Position Rotation Scale")){
  if(Header("transform",{x,y,w,22*s},"Transform",0)){y+=27*s;Txt(labelX,y+4*s,"Position",MUTED,9);AxisFields("position",fieldX,y,fieldW,n->position);y+=23*s;Txt(labelX,y+4*s,"Rotation",MUTED,9);AxisFields("rotation",fieldX,y,fieldW,n->rotation);y+=23*s;Txt(labelX,y+4*s,"Scale",MUTED,9);AxisFields("scale",fieldX,y,fieldW,n->scale,true);y+=23*s;}else y+=24*s;
 }
 if(ShowProperty("Static Mesh Materials")){
  if(Header("staticmesh",{x,y,w,22*s},"Static Mesh",1)){y+=26*s;Txt(labelX,y+14*s,"Materials",MUTED,9);Rect slot={fieldX,y,fieldW-2*s,40*s};Box(slot,BG,3,true);Thumb({slot.x+5*s,slot.y+4*s,32*s,32*s},n->mesh);Txt(slot.x+43*s,slot.y+15*s,assets[n->mesh].name,TEXT,8.5f);if(Hit("meshSlot",slot)){wantPicker=true;pickerTarget=-1;}Tip("Choose static mesh");if(ImGui::BeginDragDropTarget()){if(auto p=ImGui::AcceptDragDropPayload("ASSET")){Remember();n->mesh=*(int*)p->Data;}ImGui::EndDragDropTarget();}y+=44*s;}else y+=24*s;
 }
 if(ShowProperty("Materials Element Stone")){
  if(Header("materials",{x,y,w,22*s},"Materials",2)){y+=26*s;const char* ms[]={"M_Stone_Ruins","M_Stone_Detail","M_Stone_Warm"};for(int i=0;i<2;++i){Txt(labelX,y+13*s,"Element "+std::to_string(i),MUTED,9);Rect slot={fieldX,y,fieldW-3*s,34*s};Box(slot,BG,3,true);Image({slot.x+3*s,slot.y+2*s,29*s,29*s},1099,n->material[i]==1?402.f:365.f,30,31);Txt(slot.x+39*s,slot.y+12*s,ms[n->material[i]],TEXT,8.3f);if(Hit(("materialSlot"+std::to_string(i)).c_str(),slot))ImGui::OpenPopup(("matPicker"+std::to_string(i)).c_str());if(ImGui::BeginPopup(("matPicker"+std::to_string(i)).c_str())){for(int k=0;k<3;++k)if(ImGui::Selectable(ms[k],n->material[i]==k)){Remember();n->material[i]=k;}ImGui::EndPopup();}y+=36*s;}y+=3*s;}else y+=24*s;
 }
 if(ShowProperty("Rendering Cast Shadows Receive Shadows Visible in Game LOD Group Distance Cull")){
  if(Header("rendering",{x,y,w,22*s},"Rendering",3)){y+=27*s;PropertyBool("castShadows","Cast Shadows",labelX,y,n->cast,fieldX+17*s);y+=21*s;PropertyBool("receiveShadows","Receive Shadows",labelX,y,n->receive,fieldX+17*s);y+=21*s;PropertyBool("visibleGame","Visible in Game",labelX,y,n->inGame,fieldX+17*s);y+=25*s;Txt(labelX,y+5*s,"LOD Group",MUTED,9);const char* lods[]={"World","Small Props","Large Props","Foliage"};int oldLod=n->lod;if(Combo("lodGroup",{fieldX+15*s,y,fieldW-17*s,21*s},n->lod,lods,4)){int nv=n->lod;n->lod=oldLod;Remember();n->lod=nv;}y+=28*s;Txt(labelX,y+5*s,"Distance Cull",MUTED,9);ImGui::SetCursorScreenPos({fieldX+15*s,y+4*s});ImGui::SetNextItemWidth(fieldW-72*s);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{0,0});ImGui::SliderFloat("##distance",&n->distance,0,30000,"");ImGui::PopStyleVar();TrackEdit();ImGui::SetCursorScreenPos({x+w-52*s,y});ImGui::SetNextItemWidth(49*s);ImGui::DragFloat("##distanceNumber",&n->distance,10,0,1000000,"%.0f");TrackEdit();y+=26*s;}else y+=24*s;
 }
 if(ShowProperty("Collision Friction")){
  if(Header("collision",{x,y,w,22*s},"Collision",4)){y+=27*s;PropertyBool("collisionEnabled","Enable Collision",labelX,y,n->collision,fieldX+17*s);y+=24*s;Txt(labelX,y+5*s,"Friction",MUTED,9);ImGui::SetCursorScreenPos({fieldX+15*s,y});ImGui::SetNextItemWidth(fieldW-17*s);ImGui::SliderFloat("##friction",&n->friction,0,1,"%.2f");TrackEdit();y+=27*s;}else y+=24*s;
 }
 if(ShowProperty("Physics Simulate Gravity Mass")){
  if(Header("physics",{x,y,w,22*s},"Physics",5)){y+=27*s;PropertyBool("simulate","Simulate Physics",labelX,y,n->physics,fieldX+17*s);y+=24*s;PropertyBool("gravity","Enable Gravity",labelX,y,n->gravity,fieldX+17*s);y+=24*s;Txt(labelX,y+5*s,"Mass",MUTED,9);ImGui::SetCursorScreenPos({fieldX+15*s,y});ImGui::SetNextItemWidth(fieldW-17*s);ImGui::DragFloat("##mass",&n->mass,.1f,.001f,100000,"%.2f kg");TrackEdit();y+=28*s;}else y+=24*s;
 }
 if(ShowProperty("Gameplay Tick Actor")){
  if(Header("gameplay",{x,y,w,22*s},"Gameplay",6)){y+=27*s;PropertyBool("tickActor","Tick Actor",labelX,y,n->tick,fieldX+17*s);y+=27*s;}else y+=24*s;
 }
 if(ShowProperty("Tags")){
  if(Header("tags",{x,y,w,22*s},"Tags",7)){y+=26*s;float xx=x+10*s;int remove=-1;for(int i=0;i<(int)n->tags.size();++i){float tw=regular->CalcTextSizeA(9*s,1000,0,n->tags[i].c_str()).x+14*s;if(xx+tw>x+w-26*s){xx=x+10*s;y+=20*s;}Rect tag={xx,y,tw,17*s};Box(tag,RAISED,3);CenterText(tag,n->tags[i],MUTED,9);Hit(("tag"+std::to_string(i)).c_str(),tag);if(ImGui::BeginPopupContextItem(("tagActions"+std::to_string(i)).c_str())){if(ImGui::MenuItem("Remove tag"))remove=i;ImGui::EndPopup();}xx+=tw+4*s;}if(remove>=0){Remember();n->tags.erase(n->tags.begin()+remove);}if(IconButton("addTag",{xx,y,22*s,18*s},Icon::Plus,"Add tag"))ImGui::OpenPopup("tagPopup");if(ImGui::BeginPopup("tagPopup")){ImGui::InputTextWithHint("##newTag","Tag name",tagText,sizeof(tagText));if(ImGui::Button("Add")&&*tagText){Remember();n->tags.push_back(tagText);tagText[0]=0;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}y+=26*s;}else y+=24*s;
 }
 int removeComponent=-1;for(int i=0;i<(int)n->components.size();++i){Rect c={x,y,w,27*s};Box(c,SELECT,2);Txt(x+9*s,y+8*s,n->components[i],TEXT,9);if(IconButton(("removeComponent"+std::to_string(i)).c_str(),{x+w-26*s,y,24*s,25*s},Icon::Trash,"Remove component"))removeComponent=i;y+=33*s;}if(removeComponent>=0){Remember();n->components.erase(n->components.begin()+removeComponent);}
 if(n->locked)ImGui::EndDisabled();ImGui::SetCursorScreenPos({x,y});ImGui::Dummy({1,1});ImGui::EndChild();dl=ImGui::GetWindowDrawList();
}
void InspectorLighting(Rect r){
 float x=r.x+15*s,y=r.y+16*s,w=r.w-30*s;Txt(x,y,"Directional Light",TEXT,12);y+=21*s;Txt(x,y,"Ancient Coast · golden hour",MUTED,9);y+=33*s;
 auto slider=[&](const char* label,float& value,float low,float high,const char* format){Txt(x,y,label,MUTED,10);y+=19*s;ImGui::SetCursorScreenPos({x,y});ImGui::SetNextItemWidth(w);ImGui::SliderFloat(("##"+std::string(label)).c_str(),&value,low,high,format);y+=35*s;};
 slider("Time of Day",sunTime,0,24,"%.1f h");slider("Exposure",exposure,-2,1,"%.2f EV");slider("Sun Intensity",sunIntensity,0,10,"%.1f lux");
 ImGui::SetCursorScreenPos({x,y});ImGui::Checkbox("Ambient light",&ambient);y+=29*s;ImGui::SetCursorScreenPos({x,y});ImGui::Checkbox("Atmospheric fog",&worldFog);y+=42*s;
 Box({x,y,w,94*s},BG,4,true);Txt(x+12*s,y+12*s,"PREVIEW",CYAN,9);Txt(x+12*s,y+33*s,"Exposure and time tint the image.",MUTED,9);Txt(x+12*s,y+49*s,"Parameters are stored with the scene.",MUTED,9);Txt(x+12*s,y+65*s,"Connect them to your renderer.",MUTED,9);y+=111*s;
 if(Button("resetLighting",{x,y,w,25*s},"Restore golden hour",Icon::Sun)){exposure=0;sunTime=17.4f;sunIntensity=3.5f;ambient=worldFog=true;}
}
void InspectorWorld(Rect r){
 float x=r.x+15*s,y=r.y+16*s,w=r.w-30*s;Txt(x,y,"World Settings",TEXT,12);y+=34*s;Txt(x,y,"Scene Name",MUTED,10);y+=19*s;Input("worldName",{x,y,w,25*s},sceneName,sizeof(sceneName));y+=44*s;
 Txt(x,y,"Wind Strength",MUTED,10);y+=20*s;ImGui::SetCursorScreenPos({x,y});ImGui::SetNextItemWidth(w);ImGui::SliderFloat("##wind",&wind,0,1,"%.2f");y+=36*s;
 static int quality=2;const char* qs[]={"Low","Medium","High","Ultra"};Txt(x,y+5*s,"Preview Quality",MUTED,10);Combo("quality",{x+107*s,y,w-107*s,23*s},quality,qs,4);y+=40*s;
 Txt(x,y,"Scene Statistics",TEXT,10);y+=26*s;for(auto p:std::array<std::pair<const char*,int>,3>{{{"Scene entries",(int)nodes.size()},{"Asset library",16},{"Undo steps",(int)past.size()}}}){Txt(x,y,p.first,MUTED,9);Txt(x+w-40*s,y,std::to_string(p.second),TEXT,9);y+=25*s;}
 y+=25*s;if(Button("worldSave",{x,y,w,26*s},"Save Scene",Icon::Save))Save();y+=34*s;if(Button("worldLoad",{x,y,w,26*s},"Open Saved Scene",Icon::FolderOpen))Load();y+=34*s;if(Button("worldReset",{x,y,w,26*s},"Reset to Concept",Icon::Refresh))wantReset=true;
}
void Inspector(Rect r){
 Box(r,PANEL,4,true);float x=r.x+4*s;const char* ts[]={"Inspector","Lighting","World Settings"};Icon is[]={Icon::Info,Icon::Light,Icon::World};float ws[]={74,70,115};for(int i=0;i<3;++i){if(Tab(("inspectorTab"+std::to_string(i)).c_str(),{x,r.y,ws[i]*s,26*s},ts[i],is[i],inspectorTab==i))inspectorTab=i;x+=ws[i]*s;}
 Rect body={r.x,r.y+29*s,r.w,r.h-32*s};if(inspectorTab==0)InspectorActor(body);if(inspectorTab==1)InspectorLighting(body);if(inspectorTab==2)InspectorWorld(body);
}
void Modals(){
 if(wantAbout){ImGui::OpenPopup("About Atelier");wantAbout=false;}if(ImGui::BeginPopupModal("About Atelier",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("ATELIER / ANCIENT COAST");ImGui::Separator();ImGui::Text("Native C++ · Dear ImGui · Direct3D 11");ImGui::Text("Every surrounding panel is drawn with Dear ImGui.");ImGui::Text("The viewport and thumbnails sample your reference image.");ImGui::Text("This deliverable is an editor UI, with editable scene data.");ImGui::Text("Attach your renderer using the integration notes.");ImGui::Spacing();if(ImGui::Button("Close",{160*s,25*s}))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 if(wantHelp){ImGui::OpenPopup("Controls & shortcuts");wantHelp=false;}if(ImGui::BeginPopupModal("Controls & shortcuts",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("Ctrl+S  Save     Ctrl+O  Open     Ctrl+N  New");ImGui::Text("Ctrl+Z  Undo     Ctrl+Y  Redo     Ctrl+D  Duplicate");ImGui::Text("W / E / R  Move / Rotate / Scale");ImGui::Text("Delete  Remove actor    F2  Rename");ImGui::Text("F  Reset camera    F11  Focus viewport");ImGui::Text("Space  Play / Pause    F12  Capture PNG");ImGui::Separator();ImGui::Text("Wheel: zoom reference   Right drag: pan reference");ImGui::Text("Drag an asset onto the viewport or a scene folder.");ImGui::Text("Drag a Transform value; Ctrl+click for numeric entry.");ImGui::Text("Drag panel separators to resize the layout.");ImGui::Text("Right click actors, assets and tags for actions.");if(ImGui::Button("Close",{150*s,25*s}))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 if(wantAsset){ImGui::OpenPopup("Asset Preview");wantAsset=false;}if(ImGui::BeginPopupModal("Asset Preview",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("%s",assets[selectedAsset].name);ImVec2 p=ImGui::GetCursorScreenPos();dl=ImGui::GetWindowDrawList();Thumb({p.x,p.y,230*s,250*s},selectedAsset);ImGui::Dummy({230*s,250*s});ImGui::Text("%s · %d triangles",assets[selectedAsset].type,assets[selectedAsset].triangles);if(ImGui::Button("Add to Scene",{130*s,25*s})){AddAsset(selectedAsset);ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Close",{90*s,25*s}))ImGui::CloseCurrentPopup();ImGui::EndPopup();dl=ImGui::GetWindowDrawList();}
 if(wantPicker){ImGui::OpenPopup("Choose Static Mesh");wantPicker=false;}if(ImGui::BeginPopupModal("Choose Static Mesh",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){for(int i=0;i<16;++i)if(ImGui::Selectable(assets[i].name,Current()&&Current()->mesh==i)){if(Node* n=Current();n&&!n->locked){Remember();n->mesh=i;selectedAsset=i;}ImGui::CloseCurrentPopup();}if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 if(renaming>=0&&!ImGui::IsPopupOpen("Rename Actor"))ImGui::OpenPopup("Rename Actor");if(ImGui::BeginPopupModal("Rename Actor",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::InputText("Name",renameText,sizeof(renameText));if(ImGui::Button("Rename")&&*renameText){Remember();for(auto& n:nodes)if(n.id==renaming)n.name=renameText;renaming=-1;ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel")){renaming=-1;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
 if(wantReset){ImGui::OpenPopup("Reset Scene");wantReset=false;}if(ImGui::BeginPopupModal("Reset Scene",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("Restore the original concept scene?");ImGui::Text("Your saved .atelier file is preserved.");if(ImGui::Button("Restore")){auto before=Capture();Defaults();past.push_back(before);dirty=true;exposure=0;sunTime=17.4f;zoom=1;panX=panY=0;std::snprintf(sceneName,sizeof(sceneName),"Ancient_Coast");ImGui::CloseCurrentPopup();}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
}
void Splitter(const char* id,Rect r,float& value,float low,float high,bool vertical,bool reverse=false){Hit(id,r);if(ImGui::IsItemHovered()||ImGui::IsItemActive()){ImGui::SetMouseCursor(vertical?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);Box(r,IM_COL32(64,118,142,100),0);}if(ImGui::IsItemActive()){float d=(vertical?ImGui::GetIO().MouseDelta.x:ImGui::GetIO().MouseDelta.y)/s;value=std::clamp(value+(reverse?-d:d),low,high);}}
void Shortcuts(){if(ImGui::GetIO().WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;auto& io=ImGui::GetIO();if(io.KeyCtrl){if(ImGui::IsKeyPressed(ImGuiKey_S))Save();if(ImGui::IsKeyPressed(ImGuiKey_O))Load();if(ImGui::IsKeyPressed(ImGuiKey_N))wantReset=true;if(ImGui::IsKeyPressed(ImGuiKey_Z))Undo();if(ImGui::IsKeyPressed(ImGuiKey_Y))Redo();if(ImGui::IsKeyPressed(ImGuiKey_D))Duplicate();}else{if(ImGui::IsKeyPressed(ImGuiKey_W))tool=0;if(ImGui::IsKeyPressed(ImGuiKey_E))tool=1;if(ImGui::IsKeyPressed(ImGuiKey_R))tool=2;if(ImGui::IsKeyPressed(ImGuiKey_F)){zoom=1;panX=panY=0;}if(ImGui::IsKeyPressed(ImGuiKey_F11))focusView=!focusView;if(ImGui::IsKeyPressed(ImGuiKey_Space))playing=!playing;if(ImGui::IsKeyPressed(ImGuiKey_Delete))Delete();if(ImGui::IsKeyPressed(ImGuiKey_F2)&&Current()){renaming=selected;std::snprintf(renameText,sizeof(renameText),"%s",Current()->name.c_str());}}}
void SelfTest(){
 std::vector<std::pair<std::string,bool>> results;auto check=[&](const char* name,bool ok){results.emplace_back(name,ok);};
 auto count=nodes.size();check("reference scene and selection",Current()&&Current()->name=="Archway"&&Current()->mesh==0);
 AddAsset(3);check("asset insertion",nodes.size()==count+1&&Current()->mesh==3&&Current()->id==100);
 Undo();check("undo insertion",nodes.size()==count&&serial==100);Redo();check("redo insertion",nodes.size()==count+1&&serial==101);
 Duplicate();int duplicateId=selected;check("duplicate unique identity",Current()->id==101&&nodes.size()==count+2);
 Delete();check("delete selected actor",nodes.size()==count+1);Undo();check("undo deletion",Current()&&Current()->id==duplicateId&&nodes.size()==count+2);
 Current()->locked=true;Delete();check("locked actor protected from deletion",nodes.size()==count+2);Current()->locked=false;
 auto oldPath=statePath;std::filesystem::create_directories("outputs");statePath="outputs/selftest_scene.atelier";
 Current()->position[0]=27.125f;Current()->rotation[1]=33;Current()->mass=54.5f;Current()->tags={"test","round trip"};Current()->components={"Point Light"};Save();Current()->position[0]=-10;Load();
 check("save load transforms",Current()&&Current()->position[0]==27.125f&&Current()->rotation[1]==33);
 check("save load properties and text",Current()&&Current()->mass==54.5f&&Current()->tags.size()==2&&Current()->tags[1]=="round trip"&&Current()->components.size()==1);
 Snapshot before=Capture();Defaults();past.push_back(before);Undo();AddAsset(1);std::set<int> ids;bool unique=true;for(auto& n:nodes)if(!ids.insert(n.id).second)unique=false;check("reset undo preserves unique actor ids",unique);
 bool all=true;std::ofstream out("outputs/ui-self-test.json");out<<"{\n  \"checks\": [\n";for(size_t i=0;i<results.size();++i){all&=results[i].second;out<<"    {\"name\": \""<<results[i].first<<"\", \"passed\": "<<(results[i].second?"true":"false")<<"}"<<(i+1<results.size()?",":"")<<"\n";}out<<"  ],\n  \"passed\": "<<(all?"true":"false")<<"\n}\n";
 statePath=oldPath;Defaults();toast.clear();toastUntil=0;
}
}
void Initialize(const Texture& texture,const Texture& viewport){
 ref=texture;sceneTexture=viewport;Defaults();auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;regular=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",11.f);if(!regular)regular=io.Fonts->AddFontDefault();io.FontDefault=regular;
 ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=4;style.ChildRounding=3;style.FrameRounding=3;style.PopupRounding=5;style.ScrollbarSize=5;style.GrabMinSize=7;style.FrameBorderSize=1;style.WindowBorderSize=1;style.WindowPadding={0,0};style.FramePadding={5,3};style.ItemSpacing={4,4};style.Colors[ImGuiCol_Text]=ImColor(TEXT);style.Colors[ImGuiCol_TextDisabled]=ImColor(MUTED);style.Colors[ImGuiCol_WindowBg]=ImColor(PANEL);style.Colors[ImGuiCol_PopupBg]=ImVec4(.10f,.13f,.15f,.99f);style.Colors[ImGuiCol_Border]=ImColor(BORDER);style.Colors[ImGuiCol_FrameBg]=ImColor(BG);style.Colors[ImGuiCol_FrameBgHovered]=ImColor(RAISED);style.Colors[ImGuiCol_FrameBgActive]=ImColor(SELECT);style.Colors[ImGuiCol_CheckMark]=ImColor(TEXT);style.Colors[ImGuiCol_SliderGrab]=ImColor(TEXT);style.Colors[ImGuiCol_SliderGrabActive]=ImColor(CYAN);style.Colors[ImGuiCol_Button]=ImColor(RAISED);style.Colors[ImGuiCol_ButtonHovered]=ImColor(SELECT);style.Colors[ImGuiCol_Header]=ImColor(SELECT);style.Colors[ImGuiCol_HeaderHovered]=ImColor(RAISED);style.Colors[ImGuiCol_HeaderActive]=ImColor(SELECT);
 #ifdef _WIN32
 char* testFlag=nullptr;size_t flagSize=0;_dupenv_s(&testFlag,&flagSize,"ATELIER_SMOKE_TEST");bool runTests=testFlag&&*testFlag;std::free(testFlag);
 #else
 bool runTests=std::getenv("ATELIER_SMOKE_TEST")!=nullptr;
 #endif
 if(runTests)SelfTest();
 #ifdef _WIN32
 char* sceneFlag=nullptr;char* graphFlag=nullptr;size_t flagSizeStart=0;_dupenv_s(&sceneFlag,&flagSizeStart,"ATELIER_START_SCENE");_dupenv_s(&graphFlag,&flagSizeStart,"ATELIER_START_GRAPH");activeWorkspace=sceneFlag&&*sceneFlag?0:graphFlag&&*graphFlag?1:2;std::free(sceneFlag);std::free(graphFlag);
 #else
 activeWorkspace=std::getenv("ATELIER_START_SCENE")?0:std::getenv("ATELIER_START_GRAPH")?1:2;
 #endif
 graph::Initialize((std::filesystem::u8path(statePath).parent_path()/"BP_AncientCoast.agraph").u8string());
 material::Initialize((std::filesystem::u8path(statePath).parent_path()/"MaterialLibrary.amat").u8string());
}
void Draw(){
 if(activeWorkspace==2){int target=material::Draw();if(target)activeWorkspace=target==1?0:1;return;}
 if(activeWorkspace==1){if(graph::Draw())activeWorkspace=0;if(graph::WantsMaterials())activeWorkspace=2;return;}
 auto& io=ImGui::GetIO();s=std::clamp(std::min(io.DisplaySize.x/1280.f,io.DisplaySize.y/720.f),.75f,2.f);float w=io.DisplaySize.x,h=io.DisplaySize.y;
 ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::Begin("AtelierRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar);ImGui::SetWindowFontScale(s);ImGui::PopStyleVar();dl=ImGui::GetWindowDrawList();Box({0,0,w,h},BG,0);Shortcuts();TopBar(w);
 if(playing){animationTime+=io.DeltaTime;if(animationTime>animationDuration){if(animationLoop)animationTime=std::fmod(animationTime,animationDuration);else{animationTime=animationDuration;playing=false;}}}
 float lw=leftWidth*s,rw=rightWidth*s,bh=bottomHeight*s,cx=lw+11*s,cw=w-cx-rw-11*s,vy=40*s,vh=h-vy-bh-14*s;
 if(focusView)Viewport({6*s,vy,w-12*s,h-vy-8*s});else{
  Hierarchy({5*s,39*s,lw,h-48*s});Viewport({cx,vy,cw,vh});Content({cx,vy+vh+5*s,cw,bh});Inspector({w-rw-5*s,39*s,rw,h-48*s});
  Splitter("leftSplitter",{lw+5*s,39*s,6*s,h-48*s},leftWidth,150,std::min(330.f,(w/s-560)/2),true);
  Splitter("rightSplitter",{w-rw-11*s,39*s,6*s,h-48*s},rightWidth,240,std::min(410.f,w/s-590),true,true);
  Splitter("bottomSplitter",{cx,vy+vh,cw,5*s},bottomHeight,190,std::max(191.f,h/s-290),false,true);
 }
 Modals();dl=ImGui::GetWindowDrawList();if(toastUntil>ImGui::GetTime()){ImVec2 size=regular->CalcTextSizeA(11*s,1000,0,toast.c_str());Rect tr={w/2-size.x/2-15*s,h-43*s,size.x+30*s,29*s};Box(tr,IM_COL32(28,46,53,245),5,true);CenterText(tr,toast,TEXT,11);}
 ImGui::End();
}
void Shutdown(){material::Shutdown();graph::Shutdown();}
SceneState GetState(){
 SceneState state;state.name=sceneName;state.selectedActor=selected;state.exposure=exposure;state.timeOfDay=sunTime;state.sunIntensity=sunIntensity;state.windStrength=wind;state.ambient=ambient;state.fog=worldFog;state.playing=playing;
 for(const auto& n:nodes){ActorState a;a.id=n.id;a.parent=n.parent;a.name=n.name;a.folder=n.folder;a.meshIndex=n.mesh;a.mobility=n.mobility;a.lodGroup=n.lod;a.visible=n.visible;a.locked=n.locked;a.castShadows=n.cast;a.receiveShadows=n.receive;a.visibleInGame=n.inGame;a.collision=n.collision;a.simulatePhysics=n.physics;a.gravity=n.gravity;a.tick=n.tick;a.cullDistance=n.distance;a.mass=n.mass;a.friction=n.friction;a.tags=n.tags;a.components=n.components;for(int i=0;i<3;++i){a.position[i]=n.position[i];a.rotation[i]=n.rotation[i];a.scale[i]=n.scale[i];}a.materials={n.material[0],n.material[1]};state.actors.push_back(std::move(a));}return state;
}
bool SetSelection(int actorId){for(auto& n:nodes)if(n.id==actorId){selected=actorId;selectedAsset=n.mesh;return true;}return false;}
void SetSceneFile(const std::string& path){statePath=path;}
}
