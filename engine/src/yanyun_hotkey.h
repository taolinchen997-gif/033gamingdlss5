#pragma once
// S37 (owner 2026-09-24: 「另外快捷键能改」): the key that turns NR on and off, chosen on the panel
// and kept per user in %LOCALAPPDATA%\033YanYunRuntime\hotkey.cfg, exactly "schema=1\nkey=<code>\n"
// (a Windows virtual-key code). F11 unless the file names another offered key.
// S39 (owner: 「F11的热键修改有问题，有些键不能用」): S37 offered 26 keys and silently ignored every
// other press, and one of its keys, Pause, can never be chosen or used: the keyboard sends its press
// and release together, so no frame ever sees it held. Now letters, digits, symbols, F1-F12, arrows,
// the editing keys, the numpad and the middle / side mouse buttons can all be chosen; the few keys
// that cannot (Refusal) say why on the panel instead of doing nothing.
#include <cstring>
namespace hotkey033 {
inline constexpr int Default=0x7A; // F11
inline constexpr int F9=0x78;      // Shift+F9 is the monitor
enum class Kind:unsigned char {Function,Typing,Editing,Numpad,Mouse};
struct Key {int vk;const char* name;Kind kind;};
inline constexpr Key Keys[]={
 {0x70,"F1",Kind::Function},{0x71,"F2",Kind::Function},{0x72,"F3",Kind::Function},{0x73,"F4",Kind::Function},
 {0x74,"F5",Kind::Function},{0x75,"F6",Kind::Function},{0x76,"F7",Kind::Function},{0x77,"F8",Kind::Function},
 {0x78,"F9",Kind::Function},{0x79,"F10",Kind::Function},{0x7A,"F11",Kind::Function},{0x7B,"F12",Kind::Function},
 {0x41,"A",Kind::Typing},{0x42,"B",Kind::Typing},{0x43,"C",Kind::Typing},{0x44,"D",Kind::Typing},{0x45,"E",Kind::Typing},
 {0x46,"F",Kind::Typing},{0x47,"G",Kind::Typing},{0x48,"H",Kind::Typing},{0x49,"I",Kind::Typing},{0x4A,"J",Kind::Typing},
 {0x4B,"K",Kind::Typing},{0x4C,"L",Kind::Typing},{0x4D,"M",Kind::Typing},{0x4E,"N",Kind::Typing},{0x4F,"O",Kind::Typing},
 {0x50,"P",Kind::Typing},{0x51,"Q",Kind::Typing},{0x52,"R",Kind::Typing},{0x53,"S",Kind::Typing},{0x54,"T",Kind::Typing},
 {0x55,"U",Kind::Typing},{0x56,"V",Kind::Typing},{0x57,"W",Kind::Typing},{0x58,"X",Kind::Typing},{0x59,"Y",Kind::Typing},
 {0x5A,"Z",Kind::Typing},
 {0x30,"0",Kind::Typing},{0x31,"1",Kind::Typing},{0x32,"2",Kind::Typing},{0x33,"3",Kind::Typing},{0x34,"4",Kind::Typing},
 {0x35,"5",Kind::Typing},{0x36,"6",Kind::Typing},{0x37,"7",Kind::Typing},{0x38,"8",Kind::Typing},{0x39,"9",Kind::Typing},
 {0xC0,"`",Kind::Typing},{0xBD,"-",Kind::Typing},{0xBB,"=",Kind::Typing},{0xDB,"[",Kind::Typing},{0xDD,"]",Kind::Typing},
 {0xDC,"\\",Kind::Typing},{0xBA,";",Kind::Typing},{0xDE,"'",Kind::Typing},{0xBC,",",Kind::Typing},{0xBE,".",Kind::Typing},
 {0xBF,"/",Kind::Typing},
 {0x2D,"Insert",Kind::Editing},{0x2E,"Delete",Kind::Editing},{0x23,"End",Kind::Editing},{0x21,"PageUp",Kind::Editing},
 {0x22,"PageDown",Kind::Editing},{0x25,"方向键左",Kind::Editing},{0x26,"方向键上",Kind::Editing},{0x27,"方向键右",Kind::Editing},
 {0x28,"方向键下",Kind::Editing},{0x91,"ScrollLock",Kind::Editing},{0x5D,"Menu",Kind::Editing},
 {0x60,"Num 0",Kind::Numpad},{0x61,"Num 1",Kind::Numpad},{0x62,"Num 2",Kind::Numpad},{0x63,"Num 3",Kind::Numpad},
 {0x64,"Num 4",Kind::Numpad},{0x65,"Num 5",Kind::Numpad},{0x66,"Num 6",Kind::Numpad},{0x67,"Num 7",Kind::Numpad},
 {0x68,"Num 8",Kind::Numpad},{0x69,"Num 9",Kind::Numpad},{0x6F,"Num /",Kind::Numpad},{0x6A,"Num *",Kind::Numpad},
 {0x6D,"Num -",Kind::Numpad},{0x6B,"Num +",Kind::Numpad},{0x6E,"Num .",Kind::Numpad},
 {0x04,"鼠标中键",Kind::Mouse},{0x05,"鼠标侧键1",Kind::Mouse},{0x06,"鼠标侧键2",Kind::Mouse}};
inline const Key* Find(int vk){for(const auto& k:Keys)if(k.vk==vk)return &k;return nullptr;}
inline const char* Name(int vk){const auto* k=Find(vk);return k?k->name:nullptr;}
inline bool Allowed(int vk){return Find(vk)!=nullptr;}
// Keys that are pressed on the panel but cannot be the NR key, with the reason shown there.
struct Refusal {int vk;const char* why;};
inline constexpr Refusal Refusals[]={
 {0x24,"Home 用来打开面板，请换一个键。"},
 {0x08,"退格键打字要用，Shift+退格还用来打开面板，请换一个键。"},
 {0x0D,"回车、空格、Tab 打字和游戏里都要用，请换一个键。"},{0x20,"回车、空格、Tab 打字和游戏里都要用，请换一个键。"},
 {0x09,"回车、空格、Tab 打字和游戏里都要用，请换一个键。"},
 {0x14,"CapsLock、NumLock 会切换键盘状态，请换一个键。"},{0x90,"CapsLock、NumLock 会切换键盘状态，请换一个键。"},
 {0x2C,"PrintScreen 是 Windows 截屏键，请换一个键。"},
 {0x13,"Pause 键按下和松开是同一瞬间发出的，游戏里认不出来，请换一个键。"},
 {0xA0,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},{0xA1,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},
 {0xA2,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},{0xA3,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},
 {0xA4,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},{0xA5,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},
 {0x5B,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"},{0x5C,"Shift、Ctrl、Alt、Win 不能单独当开关键，请换一个键。"}};
inline const char* Refused(int vk){for(const auto& r:Refusals)if(r.vk==vk)return r.why;return nullptr;}
// What the player should know once the key is chosen (nullptr: nothing).
inline const char* Note(int vk){
 const auto* k=Find(vk);if(!k)return nullptr;
 if(vk==F9)return "按着 Shift 时不算，Shift+F9 仍是监控。";
 if(k->kind==Kind::Typing)return "在游戏里打字时按到它也会切换 NR。";
 if(k->kind==Kind::Numpad)return "小键盘要开着 NumLock 才认。";
 return nullptr;
}
// One switch per press, and only while the game window is in front (a letter typed in another
// program must not switch NR), not while the panel waits for a new key or one of its text fields
// takes typing, and not F9 with Shift held.
inline bool Fires(int vk,bool down,bool wasDown,bool foreground,bool shift,bool panelBlocks){
 return down&&!wasDown&&foreground&&!panelBlocks&&!(vk==F9&&shift);
}
// Anything but the exact form of an offered key keeps F11.
inline int ParseStored(const char* text){
 static constexpr char head[]="schema=1\nkey=";
 if(!text||std::strncmp(text,head,sizeof(head)-1))return Default;
 const char* p=text+sizeof(head)-1;int value=0,digits=0;
 while(*p>='0'&&*p<='9'&&digits<4){value=value*10+(*p-'0');++p;++digits;}
 if(!digits||p[0]!='\n'||p[1]!=0||!Allowed(value))return Default;
 return value;
}
}
