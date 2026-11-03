#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "Common/CommonTypes.h"
#include "Core/HW/GBAExternalKeys.h"
#define HAS_LIBMGBA 1
// Dolphin's log macros, recorded (the format string, with its level) so the tests can see each line.
static std::vector<std::string> g_logs;
#define DOLPHIN_TEST_LOG(level, f) g_logs.push_back(std::string(level) + f)
#define DEBUG_LOG_FMT(t, f, ...) DOLPHIN_TEST_LOG("D ", f)
#define INFO_LOG_FMT(t, f, ...) DOLPHIN_TEST_LOG("I ", f)
#define NOTICE_LOG_FMT(t, f, ...) DOLPHIN_TEST_LOG("N ", f)
#define WARN_LOG_FMT(t, f, ...) DOLPHIN_TEST_LOG("W ", f)
#define ERROR_LOG_FMT(t, f, ...) DOLPHIN_TEST_LOG("E ", f)
static int logged(const char* text)
{
  int n = 0;
  for (const std::string& line : g_logs)
    n += line.find(text) != std::string::npos;
  return n;
}
enum { RETRO_DEVICE_NONE=0, RETRO_DEVICE_JOYPAD=1, RETRO_DEVICE_ANALOG=5 };
enum { RETRO_DEVICE_INDEX_ANALOG_LEFT=0, RETRO_DEVICE_INDEX_ANALOG_RIGHT=1, RETRO_DEVICE_INDEX_ANALOG_BUTTON=2 };
enum { RETRO_DEVICE_ID_ANALOG_X=0, RETRO_DEVICE_ID_ANALOG_Y=1 };
enum { RETRO_DEVICE_ID_JOYPAD_B=0, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
       RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
       RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R,
       RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3 };
#define RETRO_DEVICE_GBA_EMU ((13 << 8) | RETRO_DEVICE_JOYPAD)
typedef int16_t (*retro_input_state_t)(unsigned,unsigned,unsigned,unsigned);
namespace Libretro { namespace Options {
  namespace gba {
    const char* CONTROL[4]={"c1","c2","c3","c4"};
    const char* MODIFIER[4]={"m1","m2","m3","m4"};
    const char* STICK[4]={"s1","s2","s3","s4"};
    const char* SHOW_MODE[4]={"sm1","sm2","sm3","sm4"};
    const char* SCREEN[4]={"sc1","sc2","sc3","sc4"}; const char* SIZE[4]={"sz1","sz2","sz3","sz4"};
    const char* OPACITY[4]={"op1","op2","op3","op4"}; const char* POSITION[4]={"po1","po2","po3","po4"};
    const char* FILTER[4]={"fi1","fi2","fi3","fi4"}; const char* BLEND[4]={"bl1","bl2","bl3","bl4"};
    const char* VOLUME[4]={"vo1","vo2","vo3","vo4"}; const char* SPEAKER[4]={"sp1","sp2","sp3","sp4"}; const char* SMOOTH[4]={"sm1","sm2","sm3","sm4"};
    // The buttons in Options.h's order (tools/pr-steps keeps the ones a step has).
    static const char* const names[]={"a","b","select","start","right","left","up","down","r","l"};
    constexpr int BUTTON_COUNT=sizeof(names)/sizeof(names[0]);
    static std::string keys[4][BUTTON_COUNT];
    static const char* BUTTON[4][BUTTON_COUNT];
    static bool init=[]{ for(int p=0;p<4;++p) for(int i=0;i<BUTTON_COUNT;++i){ keys[p][i]="p"+std::to_string(p+1)+"_"+names[i]; BUTTON[p][i]=keys[p][i].c_str(); } return true; }();
  }
  std::map<std::string,std::string> opt; int visible=-1;
  template<class T> T GetCached(const char* k, T d);
  template<> bool GetCached<bool>(const char* k, bool d){ auto i=opt.find(k); return i==opt.end()?d:i->second=="enabled"; }
  template<> int GetCached<int>(const char* k, int d){ auto i=opt.find(k); return i==opt.end()?d:atoi(i->second.c_str()); }
  template<> std::string GetCached<std::string>(const char* k, std::string d){ auto i=opt.find(k); return i==opt.end()?d:i->second; }
  void SetGBAOptionsVisible(bool v){ visible=v; }
  // The defaults the real options carry (Options.cpp).
  // A player's button by name ("show"), as Options.h's BUTTON[player][index].
  static const char* B(int p, const char* name){ for(int i=0;i<gba::BUTTON_COUNT;++i) if(std::string(gba::names[i])==name) return gba::BUTTON[p][i]; std::abort(); }
  static void defaults(){ opt.clear();   // the layout's ten default to themselves (Select on L2), the rest to none
    for(int p=0;p<4;++p) for(int i=0;i<gba::BUTTON_COUNT;++i){ std::string n=gba::names[i]; opt[gba::BUTTON[p][i]]= n=="select"?"l2": i<10?n:"none"; } }
}}
namespace Libretro { namespace GBALink {
  std::vector<std::string> calls;
  void PollHardcore(){}
  void ApplyRoms(const unsigned*, unsigned){}
  void ToggleLinkCable(int port){ calls.push_back("link"+std::to_string(port)); }
  int volume[4]={-1,-1,-1,-1}; bool muted[4]{};
  void SetVolume(int port, int percent, bool m){ volume[port]=percent; muted[port]=m; }
  void QuickState(int port, bool save){ calls.push_back((save?"save":"load")+std::to_string(port)); }
  void ScanCard(int port){ calls.push_back("scan"+std::to_string(port)); }
}}
namespace Libretro { namespace GBAScreen {
  struct Window { std::atomic<bool> shown{false}; std::atomic<int> x{0}, y{0}, opacity{100}, size{1}; std::atomic<bool> soft{false}, blend{false}; };
  Window windows[4]; Window& Get(int p){ return windows[p&3]; }
}}
namespace Libretro { namespace GBASound {
  int route[4]={-9,-9,-9,-9}; void Route(int port, int player){ route[port]=player; }
  int smooth[4]={-1,-1,-1,-1}; void SetSmooth(int port, bool s){ smooth[port]=s; }
}}
namespace Libretro { namespace Input {
static bool down[8][16];
static int16_t stick[8][2];
static retro_input_state_t input_cb = [](unsigned port,unsigned dev,unsigned idx,unsigned id)->int16_t{
  if(dev==RETRO_DEVICE_ANALOG && idx==RETRO_DEVICE_INDEX_ANALOG_LEFT) return stick[port][id];
  return (dev==RETRO_DEVICE_JOYPAD||(dev==RETRO_DEVICE_ANALOG&&idx==RETRO_DEVICE_INDEX_ANALOG_BUTTON))&&down[port][id]; };
static unsigned input_types[8];
//@@BLOCK@@
}}
using namespace Libretro::Input;
static int fails=0;
#define CHECK(c, msg) do{ bool ok=(c); printf("%s %s\n", ok?"PASS":"FAIL", msg); if(!ok) ++fails; }while(0)
static void clear(){ for(auto& p: down) for(bool& b: p) b=false; for(auto& s: stick) s[0]=s[1]=0; }
static int16_t gc(unsigned port, unsigned id){ return DolphinInput(port,RETRO_DEVICE_JOYPAD,0,id); }
static u16 keys(int port){ return HW::GBA::PadKeys(port).load(); }
int main(){
  using namespace Libretro::Options;
  using Libretro::GBALink::calls;
  defaults();
  clear(); down[0][RETRO_DEVICE_ID_JOYPAD_UP]=true; down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; UpdateGBAInput();
  CHECK(keys(1)==0, "no GBA port: no keys");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==1, "no GBA port: L3 reaches the GameCube pad");
  CHECK(visible==0, "no GBA port: GBA options hidden");

  input_types[1]=RETRO_DEVICE_GBA_EMU; clear(); UpdateGBAInput();
  CHECK(visible==1, "GBA port present: options shown");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==0, "player 1's modifier (L3) never reaches the GameCube pad");
  down[0][RETRO_DEVICE_ID_JOYPAD_UP]=true; UpdateGBAInput();
  CHECK(keys(1)==0, "D-pad Up alone: not a GBA key");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_UP)==1, "D-pad Up alone: still moves the GameCube pad");
  down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[0][RETRO_DEVICE_ID_JOYPAD_A]=true; down[0][RETRO_DEVICE_ID_JOYPAD_L2]=true; down[0][RETRO_DEVICE_ID_JOYPAD_START]=true; UpdateGBAInput();
  CHECK(keys(1)==(0x40|0x01|0x04|0x08), "L3 held + Up, A, L2, Start = GBA Up, A, Select, Start (0x4d)");
  for (unsigned id : {(unsigned)RETRO_DEVICE_ID_JOYPAD_UP,(unsigned)RETRO_DEVICE_ID_JOYPAD_A,(unsigned)RETRO_DEVICE_ID_JOYPAD_L2,(unsigned)RETRO_DEVICE_ID_JOYPAD_START})
    CHECK(gc(0,id)==0, "layout button hidden from the GameCube pad while held");
  CHECK(DolphinInput(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_BUTTON,RETRO_DEVICE_ID_JOYPAD_L2)==0, "analog-button L2 hidden too");
  down[0][RETRO_DEVICE_ID_JOYPAD_X]=true; UpdateGBAInput();
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_X)==1, "X is not in the layout: reaches the GameCube pad");
  stick[0][RETRO_DEVICE_ID_ANALOG_X]=30000; UpdateGBAInput();
  CHECK((keys(1)&0x10)!=0, "left stick right = GBA Right");
  CHECK(DolphinInput(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X)==0, "left stick hidden from the GameCube pad while held");
  down[0][RETRO_DEVICE_ID_JOYPAD_L3]=false; UpdateGBAInput();
  CHECK(keys(1)==0 && gc(0,RETRO_DEVICE_ID_JOYPAD_UP)==1 && DolphinInput(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_X)==30000,
        "modifier released: D-pad and stick reach the GameCube pad again");

  clear(); opt[gba::STICK[0]]="disabled"; down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; stick[0][RETRO_DEVICE_ID_ANALOG_Y]=-30000; UpdateGBAInput();
  CHECK(keys(1)==0 && DolphinInput(0,RETRO_DEVICE_ANALOG,RETRO_DEVICE_INDEX_ANALOG_LEFT,RETRO_DEVICE_ID_ANALOG_Y)==-30000, "stick as D-pad off: stick stays the GameCube's");
  opt[gba::STICK[0]]="enabled";

  clear(); opt[B(0,"a")]="x"; opt[B(0,"b")]="none"; down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[0][RETRO_DEVICE_ID_JOYPAD_X]=true; down[0][RETRO_DEVICE_ID_JOYPAD_B]=true; UpdateGBAInput();
  CHECK(keys(1)==0x01, "rebound: X = GBA A; B unbound does nothing");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_X)==0 && gc(0,RETRO_DEVICE_ID_JOYPAD_B)==1, "rebound X hidden, unbound B reaches the GameCube pad");
  defaults();

  clear(); opt[gba::MODIFIER[0]]="r2"; down[0][RETRO_DEVICE_ID_JOYPAD_R2]=true; down[0][RETRO_DEVICE_ID_JOYPAD_L]=true; down[0][RETRO_DEVICE_ID_JOYPAD_L2]=true; UpdateGBAInput();
  CHECK(keys(1)==(0x200|0x04), "modifier R2: L1 = GBA L, L2 = GBA Select (0x204)");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_R2)==0, "R2 as modifier is hidden from the GameCube pad");

  clear(); opt[gba::MODIFIER[0]]="always"; down[0][RETRO_DEVICE_ID_JOYPAD_A]=true; down[0][RETRO_DEVICE_ID_JOYPAD_X]=true; UpdateGBAInput();
  CHECK(keys(1)==0x01, "Always: A is GBA A with nothing held");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_X)==0, "Always: the controller reaches the GameCube pad not at all");
  opt[gba::MODIFIER[0]]="l3";

  clear(); opt[gba::CONTROL[1]]="p2"; down[1][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[1][RETRO_DEVICE_ID_JOYPAD_DOWN]=true; down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; UpdateGBAInput();
  CHECK(keys(1)==0x80, "played by player 2: their Down -> GBA Down");
  CHECK(gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==1, "player 1 plays no GBA: their L3 reaches the GameCube pad");

  clear(); opt[gba::CONTROL[1]]="own"; down[1][RETRO_DEVICE_ID_JOYPAD_A]=true; UpdateGBAInput();
  CHECK(keys(1)==0x01, "port's own controller: A with no modifier");

  clear(); opt[gba::CONTROL[1]]="nobody"; down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[0][RETRO_DEVICE_ID_JOYPAD_A]=true; down[1][RETRO_DEVICE_ID_JOYPAD_A]=true; UpdateGBAInput();
  CHECK(keys(1)==0 && gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==1, "nobody: no keys, nothing hidden");

  input_types[2]=RETRO_DEVICE_GBA_EMU; opt[gba::CONTROL[1]]="p1"; opt[gba::CONTROL[2]]="p1"; clear(); down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[0][RETRO_DEVICE_ID_JOYPAD_B]=true; UpdateGBAInput();
  CHECK(keys(1)==0x02 && keys(2)==0x02, "player 1 plays two GBAs: both get B");

  CHECK(logged("played by player {}{}") >= 1 && logged("played by player {}{}") <= 4, "who plays each GBA is logged on a change, not every frame");
  input_types[2]=RETRO_DEVICE_JOYPAD; defaults();

  HW::GBA::FrontendOwnsInput().store(true); clear(); down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; down[0][RETRO_DEVICE_ID_JOYPAD_A]=true; UpdateGBAInput();
  CHECK(keys(1)==0 && gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==1, "a frontend maps the GBA itself: the core reads nothing");
  HW::GBA::FrontendOwnsInput().store(false);

  input_types[1]=RETRO_DEVICE_JOYPAD; clear(); down[0][RETRO_DEVICE_ID_JOYPAD_L3]=true; UpdateGBAInput();
  CHECK(visible==0 && keys(1)==0 && gc(0,RETRO_DEVICE_ID_JOYPAD_L3)==1, "GBA port removed: options hidden, keys cleared, masks off");
  CHECK(logged("GBA: options {}") == 3, "the options shown / hidden is logged on a change only (hidden, shown, hidden)");
  CHECK(logged("played by nobody") >= 1, "nobody playing a GBA is logged");
  printf(fails?"\nFAILED %d\n":"\nALL PASSED\n", fails); return fails?1:0;
}
