/*
  xdrv_01_webserver.ino - webserver for Tasmota

  Copyright (C) 2021  Theo Arends and Adrian Scillato

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_WEBSERVER
/*********************************************************************************************\
 * Web server and WiFi Manager
 *
 * Enables configuration and reconfiguration of WiFi credentials using a Captive Portal
 * Based on source by AlexT (https://github.com/tzapu)
\*********************************************************************************************/

#define XDRV_01                                   1

#ifndef WIFI_SOFT_AP_CHANNEL
#define WIFI_SOFT_AP_CHANNEL                      1      // Soft Access Point Channel number between 1 and 11 as used by WifiManager web GUI
#endif

#ifndef MAX_WIFI_NETWORKS_TO_SHOW
#define MAX_WIFI_NETWORKS_TO_SHOW                 5      // Maximum number of Wifi Networks to show in the Wifi Configuration Menu BEFORE clicking on Show More Networks.
#endif

#ifndef RESTART_AFTER_INITIAL_WIFI_CONFIG
#define RESTART_AFTER_INITIAL_WIFI_CONFIG         true   // Restart Tasmota after initial Wifi Config of a blank device
#endif                                                   //   If disabled, Tasmota will keep both the wifi AP and the wifi connection to the router
                                                         //   but only until next restart.
#ifndef AFTER_INITIAL_WIFI_CONFIG_GO_TO_NEW_IP           // If RESTART_AFTER_INITIAL_WIFI_CONFIG and AFTER_INITIAL_WIFI_CONFIG_GO_TO_NEW_IP are true,
#define AFTER_INITIAL_WIFI_CONFIG_GO_TO_NEW_IP    true   //   the user will be redirected to the new IP of Tasmota (in the new Network).
#endif                                                   //   If the first is true, but this is false, the device will restart but the user will see
                                                         //   a window telling that the WiFi Configuration was Ok and that the window can be closed.

const uint16_t CHUNKED_BUFFER_SIZE = 1000;                // Chunk buffer size (needs to be well below stack space (8k for ESP32) but large enough to cache some small messages)

const uint16_t HTTP_REFRESH_TIME = 2000;                 // milliseconds
const uint16_t HTTP_RESTART_RECONNECT_TIME = 10000;      // milliseconds - Allow time for restart and wifi reconnect
const uint16_t HTTP_OTA_RESTART_RECONNECT_TIME = 15000;  // milliseconds - Allow time for restart and wifi reconnect

#include <ESP8266WebServer.h>
#include <DNSServer.h>

const char HTTP_HEADER1[] PROGMEM =
  "<!DOCTYPE html><html lang=\"%s\" class=\"\">"
  "<head>"
  "<meta charset='utf-8'>"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>"
  "<link rel=\"icon\" href=\"data:image/x-icon;base64,AAABAAEAEBACAAEAAQCwAAAAFgAAACgAAAAQAAAAIAAAAAEAAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA////AP5/b+H6X2/h8k9v4eZnb+Hud2/h7ndv4e53b+FmZm/hMkxv4ZgZb+HOc2/h5+dv4fPPb+H5n2/h/D9v4f5/b+EAAO4EAADuBAAA7gQAAO4EAADuBAAA7gQAAO4EAADuBAAA7gQAAO4EAADuBAAA7gQAAO4EAADuBAAA7gQAAO4E\">"
  "<title>%s %s</title>";

const char HTTP_SCRIPT_CORE[] PROGMEM =
  "var x=null,lt,to,tp,pc='';"
  "eb=s=>document.getElementById(s);"
  "qs=s=>document.querySelector(s);"
  "sp=i=>eb(i).type=(eb(i).type==='text'?'password':'text');"
  "wl=f=>window.addEventListener('load',f);"

  "function tsSetDisp(i,v){var e=eb(i);if(e)e.style.display=v;}"
  "function tsSetRot(i,on){var e=eb(i);if(e)e.className=on?'ts-chevron rot':'ts-chevron';}"
  "function tsCloseMenus(){"
    "tsSetDisp('ts-nav','none');"
    "tsSetDisp('ts-sub-cfg','none');"
    "tsSetDisp('ts-sub-info','none');"
    "tsSetDisp('ts-sub-maint','none');"
    "tsSetRot('ts-ch-cfg',false);"
    "tsSetRot('ts-ch-info',false);"
    "tsSetRot('ts-ch-maint',false);"
  "}"
  "function tsToggleNav(ev){"
    "if(ev)ev.stopPropagation();"
    "var n=eb('ts-nav');"
    "if(!n)return false;"
    "var open=('block'===n.style.display);"
    "tsCloseMenus();"
    "if(!open){n.style.display='block';}"
    "return false;"
  "}"
  "function tsToggleSub(id,ch,ev){"
    "if(ev)ev.stopPropagation();"
    "var e=eb(id);"
    "if(!e)return false;"
    "var open=('grid'===e.style.display);"
    "tsSetDisp('ts-sub-cfg','none');"
    "tsSetDisp('ts-sub-info','none');"
    "tsSetDisp('ts-sub-maint','none');"
    "tsSetRot('ts-ch-cfg',false);"
    "tsSetRot('ts-ch-info',false);"
    "tsSetRot('ts-ch-maint',false);"
    "if(!open){"
      "e.style.display='grid';"
      "tsSetRot(ch,true);"
    "}"
    "return false;"
  "}"
  "function tsDocClick(ev){"
    "var nav=eb('ts-nav'),btn=eb('ts-nav-btn');"
    "if(!nav||!btn)return;"
    "if('block'!==nav.style.display)return;"
    "if(nav.contains(ev.target)||btn.contains(ev.target))return;"
    "tsCloseMenus();"
  "}"
  "function tsInit(){"
    "tsCloseMenus();"
    "document.addEventListener('click',tsDocClick);"
    "document.addEventListener('keydown',function(e){if('Escape'===e.key)tsCloseMenus();});"
  "}"
  "wl(tsInit);"

  "function jd(){"
    "var t=0,i=document.querySelectorAll('input,button,textarea,select');"
    "while(i.length>=t){"
      "if(i[t]){"
        "i[t]['name']=(i[t].hasAttribute('id')&&(!i[t].hasAttribute('name')))?i[t]['id']:i[t]['name'];"
      "}"
      "t++;"
    "}"
  "}"
  "function sf(s){"
    "var t=0,i=document.querySelectorAll('.hf');"
    "while(i.length>=t){"
      "if(i[t]){"
        "i[t].style.display=s?'block':'none';"
      "}"
      "t++;"
    "}"
  "}"
  "wl(jd);";

const char HTTP_SCRIPT_UPLOAD[] PROGMEM =
  "function su(t){"
    "eb('f3').style.display='none';"
    "eb('f2').style.display='block';"
    "t.form.submit();"
  "}"
  "function upl(t){"
    "var sl=t.form['u2'].files[0].slice(0,1);"
    "var rd=new FileReader();"
    "rd.onload=()=>{"
      "var bb=new Uint8Array(rd.result);"
      "if(bb.length==1&&bb[0]==0xE9){"
        "fct(t);"
      "}else{"
        "t.form.submit();"
      "};"
    "};"
    "rd.readAsArrayBuffer(sl);"
    "return false;"
  "};"
  "function fct(t){"
    "var x=new XMLHttpRequest();"
    "x.open('GET','/u4?u4=fct&api=',true);"
    "x.onreadystatechange=()=>{"
      "if(x.readyState==4&&x.status==200){"
        "var s=x.responseText;"
        "if(s=='false')setTimeout(()=>{fct(t);},6000);"
        "if(s=='true')setTimeout(()=>{su(t);},1000);"
      "}else if(x.readyState==4&&x.status==0){"
        "setTimeout(()=>{fct(t);},2000);"
      "};"
    "};"
    "x.send();"
  "}";


const char HTTP_SCRIPT_COUNTER[] PROGMEM =
  "var cn=180;"
  "function u(){"
    "if(cn>=0){"
      "var e=eb('t');"
      "if(e)e.innerHTML='" D_RESTART_IN " '+cn+' " D_SECONDS "';"
      "cn--;"
      "setTimeout(u,1000);"
    "}"
  "}"
  "wl(u);";


const char HTTP_SCRIPT_ROOT[] PROGMEM =
  "var tsrf={xhr:null,lt:0,ft:0};"
  "function tsRfIds(){"
    "var n=document.querySelectorAll('[data-tsrf=\"1\"][id]'),a=[],i;"
    "for(i=0;i<n.length;i++){a.push(n[i].id);}"
    "return a.join(',');"
  "}"
  "function tsRfFmt(s){"
    "return s.replace(/{t}/g,\"<table style='width:100%%'>\")"
            ".replace(/{s}/g,\"<tr><th>\")"
            ".replace(/{m}/g,\"</th><td style='width:20px;white-space:nowrap'>\")"
            ".replace(/{e}/g,\"</td></tr>\");"
  "}"
  "function tsRfApply(s){"
    "var b='~#RF#~',m='~#RM#~',e='~#RE#~',p=0;"
    "while(true){"
      "var i=s.indexOf(b,p);"
      "if(i<0)break;"
      "var j=s.indexOf(m,i+b.length);"
      "if(j<0)break;"
      "var k=s.indexOf(e,j+m.length);"
      "if(k<0)break;"
      "var id=s.substring(i+b.length,j);"
      "var html=s.substring(j+m.length,k);"
      "if(id==='@js'){"
        "try{(new Function(html))();}catch(ex){}"
      "}else{"
        "var el=eb(id);"
        "if(el){el.innerHTML=tsRfFmt(html);}"
      "}"
      "p=k+e.length;"
    "}"
  "}"
  "function la(p){"
    "var a=p||'',ids=tsRfIds(),u='?m=1';"
    "if(!ids&&!a){return;}"
    "clearTimeout(tsrf.ft);"
    "clearTimeout(tsrf.lt);"
    "if(tsrf.xhr!=null){tsrf.xhr.abort();}"
    "if(ids){u+='&rf='+encodeURIComponent(ids);}"
    "u+=a;"
    "tsrf.xhr=new XMLHttpRequest();"
    "tsrf.xhr.onreadystatechange=()=>{"
      "if(tsrf.xhr.readyState==4&&tsrf.xhr.status==200){"
        "tsRfApply(tsrf.xhr.responseText);"
        "clearTimeout(tsrf.ft);"
        "clearTimeout(tsrf.lt);"
        "tsrf.lt=setTimeout(la,%d);"
      "}"
    "};"
    "tsrf.xhr.open('GET',u,true);"
    "tsrf.xhr.send();"
    "tsrf.ft=setTimeout(la,2e4);"
  "}";


const char HTTP_SCRIPT_ROOT_PART2[] PROGMEM =
  "function lc(v,i,p){"
    "if(eb('s')){"                        // Check if Saturation is in DOM otherwise javascript fails on la()
      "if(v=='h'||v=='d'){"               // Hue or Brightness changed so change Saturation colors too
        "var sl=eb('sl4').value;"
        "eb('s').style.background='linear-gradient(to right,rgb('+sl+'%%,'+sl+'%%,'+sl+'%%),hsl('+eb('sl2').value+',100%%,50%%))';"
      "}"
    "}"
    "la('&'+v+i+'='+p);"
  "}";

const char HTTP_SCRIPT_ROOT_AUTOLOAD[] PROGMEM =
  "wl(la);";



const char HTTP_SCRIPT_WIFI[] PROGMEM =
  "function c(l){"
    "eb('s1').value=l.innerText||l.textContent;"
    "eb('p1').focus();"
  "}";

const char HTTP_SCRIPT_HIDE[] PROGMEM =
  "function hidBtns(){"
    "if(eb('butmo'))eb('butmo').style.display='none';"
    "if(eb('butmod'))eb('butmod').style.display='none';"
    "if(eb('wm-restart'))eb('wm-restart').style.display='block';"
    "if(eb('wm-reset'))eb('wm-reset').style.display='block';"
    "if(eb('wm-restore'))eb('wm-restore').style.display='block';"
  "}";

const char HTTP_SCRIPT_RELOAD_TIME[] PROGMEM =
  "setTimeout(function(){location.href='.';},%d);";

const char HTTP_SCRIPT_CONSOL[] PROGMEM =
  "var sn=0,id=0,ft,ltm=%d;"                      // Scroll position, Get most of weblog initially
  "function l(p){"                        // Console log and command service
    "var c,o='';"
    "clearTimeout(lt);"
    "clearTimeout(ft);"
    "t=eb('t1');"
    "if(p==1){"
      "c=eb('c1');"                       // Console command id
      "o='&c1='+encodeURIComponent(c.value);"
      "c.value='';"
      "t.scrollTop=1e8;"
      "sn=t.scrollTop;"
    "}"
    "if(t.scrollTop>=sn){"                // User scrolled back so no updates
      "if(x!=null){x.abort();}"           // Abort if no response within 2 seconds (happens on restart 1)
      "x=new XMLHttpRequest();"
      "x.onreadystatechange=()=>{"
        "if(x.readyState==4&&x.status==200){"
          "var z,d;"
          "d=x.responseText.split(/}1/);"  // Field separator
          "id=d.shift();"
          "if(d.shift()==0){t.value='';}"
          "z=d.shift();"
          "if(z.length>0){t.value+=z;}"
          "t.scrollTop=1e8;"
          "sn=t.scrollTop;"
          "clearTimeout(ft);"
          "lt=setTimeout(l,ltm);" // webrefresh timer....
        "}"
      "};"
      "x.open('GET','cs?c2='+id+o,true);"  // Related to Webserver->hasArg("c2") and WebGetArg("c2", stmp, sizeof(stmp))
      "x.send();"
      "ft=setTimeout(l,2e4);" // fail timeout, triggered 20s after asking for XHR
    "}else{"
      "lt=setTimeout(l,ltm);" // webrefresh timer....
    "}"
    "return false;"
  "}"
  "wl(l);"                                // Load initial console text

  // Console command history
  "var hc=[],cn=0;"                       // hc = History commands, cn = Number of history being shown
  "function h(){"
//    "if(!(navigator.maxTouchPoints||'ontouchstart'in document.documentElement)){eb('c1').autocomplete='off';}"  // No touch so stop browser autocomplete
    "eb('c1').addEventListener('keydown',e=>{"
      "var b=eb('c1'),c=e.keyCode;"       // c1 = Console command id
      "if(38==c||40==c){" // ArrowUp or ArrowDown
        "b.autocomplete='off';" // ArrowUp or ArrowDown must be a keyboard so stop browser autocomplete
        "setTimeout(b=>{" // for best compatibility (chrome) we need to schedule this function
          "b.focus();" // for best compatibility (chrome) we need to (re)focus the input element
          "b.setSelectionRange(1e9,1e9)" // move cursor to the end (hopefully) of the command inserted from history
        "},0,b)"
      "}"
      "38==c?(++cn>hc.length&&(cn=hc.length),b.value=hc[cn-1]||''):"   // ArrowUp
      "40==c?(0>--cn&&(cn=0),b.value=hc[cn-1]||''):"                   // ArrowDown
      "13==c&&(hc.length>19&&hc.pop(),hc.unshift(b.value),cn=0)"       // Enter, 19 = Max number -1 of commands in history
    "});"
  "}"
  "wl(h);";                               // Add console command key eventlistener after name has been synced with id (= wl(jd))





const char HTTP_SCRIPT_INFO_BEGIN[] PROGMEM =
  "function i(){"
    "var s,o=\"";

const char HTTP_SCRIPT_INFO_END[] PROGMEM =
    "\";"
    "s=o.replace(/}1/g,\"</td></tr><tr><th>\").replace(/}2/g,\"</th><td>\");"
    "eb('i').innerHTML=s;"
  "}"
  "window.addEventListener('load',i);";

// SCRIPTS

const char HTTP_HEAD_STYLE_ROOT_COLOR[] PROGMEM =
  "<style>"
  ":root{"
  "--c_bg:#%06x;"
  "--c_frm:#%06x;"
  "--c_ttl:#%06x;"
  "--c_txt:#%06x;"
  "--c_txtwrn:#%06x;"
  "--c_txtscc:#%06x;"
  "--c_btn:#%06x;"
  "--c_btnoff:#%06x;"
  "--c_btntxt:#%06x;"
  "--c_btnhvr:#%06x;"
  "--c_btnrst:#%06x;"
  "--c_btnrsthvr:#%06x;"
  "--c_btnsv:#%06x;"
  "--c_btnsvhvr:#%06x;"
  "--c_in:#%06x;"
  "--c_intxt:#%06x;"
  "--c_csl:#%06x;"
  "--c_csltxt:#%06x;"
  "--c_tab:#%06x;"
  "--c_tabtxt:#%06x;"
  "}";

const char HTTP_HEAD_STYLE1[] PROGMEM =
  "html{height:100%;box-sizing:border-box;overflow-x:hidden;}"
  "*,*::before,*::after{box-sizing:inherit;}"
  "body{margin:0;padding:0;min-height:100%;text-align:center;font-family:verdana,sans-serif;background:var(--c_bg);color:var(--c_txt);overflow-x:hidden;}"  // COLOR_BACKGROUND
  "main,header,section,nav,div,fieldset,input,select,textarea,button,table,tr,td,th{box-sizing:border-box;}"
  "div,fieldset,input,select{padding:5px;font-size:1em;}"
  "fieldset{background:var(--c_frm);color:var(--c_txt);}"  // COLOR_FORM
  "legend,label,span,p,td{color:var(--c_txt);}"
  "th,h1,h2,h3,h4{color:var(--c_ttl);}"
  "table{color:var(--c_txt);}"
  "p{margin:0.5em 0;}"
  "input{width:100%;box-sizing:border-box;-webkit-box-sizing:border-box;-moz-box-sizing:border-box;background:var(--c_in);color:var(--c_intxt);}"  // COLOR_INPUT, COLOR_INPUT_TEXT
  "input[type=checkbox],input[type=radio]{width:1em;margin-right:6px;vertical-align:-1px;}"
  "input[type=range]{width:99%;}"
  "select{width:100%;background:var(--c_in);color:var(--c_intxt);}"  // COLOR_INPUT, COLOR_INPUT_TEXT
  "textarea{resize:vertical;width:100%;height:318px;padding:5px;overflow:auto;background:var(--c_csl);color:var(--c_csltxt);}"  // COLOR_CONSOLE, COLOR_CONSOLE_TEXT
  "td{padding:0px;}";

const char HTTP_HEAD_STYLE2[] PROGMEM =
  "button{border:0;border-radius:0.3rem;background:var(--c_btn);color:var(--c_btntxt);line-height:2.4rem;font-size:1.2rem;width:100%;-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer;}"  // COLOR_BUTTON, COLOR_BUTTON_TEXT
  "button:hover{background:var(--c_btnhvr);}"  // COLOR_BUTTON_HOVER
  ".bred{background:var(--c_btnrst);}"  // COLOR_BUTTON_RESET
  ".bred:hover{background:var(--c_btnrsthvr);}"  // COLOR_BUTTON_RESET_HOVER
  ".bgrn{background:var(--c_btnsv);}"  // COLOR_BUTTON_SAVEWifiManager
  ".bgrn:hover{background:var(--c_btnsvhvr);}"  // COLOR_BUTTON_SAVE_HOVER
  "a{color:var(--c_btn);text-decoration:none;}"  // COLOR_BUTTON
  ".p{float:left;text-align:left;}"
  ".q{float:right;text-align:right;}"
  ".r{border-radius:0.3em;padding:2px;margin:4px 2px;}"
  ".hf{display:none;}"
  ".ts-brand{width:auto;}";

const char HTTP_HEAD_STYLE_SHELL[] PROGMEM =
  ".ts-shell{min-height:100vh;width:100%;margin:0;padding:0;}"
  ".ts-topbar{"
    "position:sticky;top:0;left:0;z-index:1000;"
    "height:80px;width:100%;"
    "display:flex;align-items:center;justify-content:space-between;gap:12px;"
    "margin:0;padding:0 14px;"
    "background:rgba(20,20,20,.86);"
    "backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);"
    "border-bottom:1px solid rgba(255,255,255,.08);"
    "box-shadow:0 10px 28px rgba(0,0,0,.22);"
  "}"
  ".ts-topbar-left{display:flex;align-items:center;gap:0;min-width:0;position:relative;}"
  ".ts-brand{"
    "width:auto;min-width:138px;max-width:220px;height:52px;"
    "padding:0 20px;"
    "border-radius:16px;"
    "font-size:1rem;font-weight:700;line-height:1;"
    "display:inline-flex;align-items:center;justify-content:center;"
    "background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(0,0,0,.05)),var(--c_btn);"
    "box-shadow:0 10px 24px rgba(0,0,0,.18);"
  "}"
  ".ts-brand:hover{transform:translateY(-1px);}"
  ".ts-topbar-right{display:flex;align-items:center;justify-content:flex-end;gap:8px;flex-wrap:wrap;min-width:0;}"
  ".ts-status{"
    "display:inline-flex;align-items:center;gap:6px;"
    "height:34px;padding:0 10px;"
    "border-radius:999px;"
    "background:rgba(255,255,255,.06);"
    "border:1px solid rgba(255,255,255,.10);"
    "font-size:12px;font-weight:700;color:var(--c_txt);"
  "}"
  ".ts-status.mono{font-family:Consolas,Monaco,monospace;font-weight:600;}"
  ".ts-status .wifi{margin-right:2px;}"
  ".ts-content{width:min(1180px,calc(100vw - 20px));margin:14px auto 28px;padding:0;text-align:left;}"
  ".ts-hostline{margin:0 0 12px;padding:0 2px;font-size:.9rem;color:var(--c_ttl);opacity:.86;line-height:1.4;}"
  ".ts-menu{"
    "display:none;"
    "position:absolute;top:64px;left:0;"
    "width:min(340px,calc(100vw - 24px));"
    "max-height:calc(100vh - 108px);overflow:auto;"
    "padding:10px;"
    "border-radius:20px;"
    "background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(0,0,0,.08)),var(--c_frm);"
    "border:1px solid rgba(255,255,255,.10);"
    "box-shadow:0 18px 46px rgba(0,0,0,.34);"
    "text-align:left;"
  "}"
  ".ts-menu-group+.ts-menu-group{margin-top:10px;padding-top:10px;border-top:1px solid rgba(255,255,255,.07);}"
  ".ts-menu-link,.ts-menu-toggle{"
    "display:flex;align-items:center;justify-content:space-between;gap:10px;"
    "width:100%;padding:12px 14px;"
    "border:0;border-radius:14px;"
    "background:transparent;color:var(--c_txt);"
    "text-align:left;line-height:1.25;font-size:14px;"
    "box-shadow:none;"
  "}"
  ".ts-menu-link:hover,.ts-menu-toggle:hover{background:rgba(255,255,255,.06);transform:none;}"
  ".ts-menu-copy{display:block;min-width:0;}"
  ".ts-menu-label{display:block;font-weight:700;color:var(--c_ttl);}"
  ".ts-menu-note{display:block;margin-top:2px;font-size:12px;opacity:.72;}"
  ".ts-menu-sub{margin:6px 0 0 14px;padding-left:10px;border-left:1px solid rgba(255,255,255,.10);display:none;gap:6px;}"
  ".ts-sub-link{display:block;padding:10px 12px;border-radius:12px;color:var(--c_txt);font-size:13px;line-height:1.3;}"
  ".ts-sub-link:hover{background:rgba(255,255,255,.06);}"
  ".ts-chevron{font-size:12px;opacity:.72;transition:transform .16s ease;}"
  ".ts-chevron.rot{transform:rotate(90deg);}"
  "@media (max-width:720px){"
    ".ts-topbar{padding:0 10px;gap:8px;}"
    ".ts-brand{min-width:118px;padding:0 16px;height:48px;}"
    ".ts-topbar-right{gap:6px;}"
    ".ts-status{padding:0 8px;font-size:11px;}"
    ".ts-content{width:calc(100vw - 12px);margin:12px auto 20px;}"
    ".ts-menu{top:58px;left:0;width:min(320px,calc(100vw - 16px));}"
  "}";

const char HTTP_HEAD_STYLE_SHELL2[] PROGMEM =
  ".ts-info-wrap table{width:100%%;border-collapse:collapse;}"
  ".ts-info-wrap th{padding-right:8px;text-align:left;color:var(--c_ttl);vertical-align:top;}"
  ".ts-info-wrap td{color:var(--c_txt);vertical-align:top;word-break:break-word;}"
  ".ts-table{width:100%;border-collapse:collapse;}"
  ".ts-table th,.ts-table td{padding:10px 12px;text-align:left;vertical-align:top;border-top:1px solid rgba(255,255,255,.08);}"
  ".ts-table tr:first-child th,.ts-table tr:first-child td{border-top:0;}"
  ".ts-table th{color:var(--c_ttl);}"
  ".ts-table td{color:var(--c_txt);word-break:break-word;}"
  ".ts-pagehead{margin:0 0 16px;padding:2px 2px 6px;}"
  ".ts-pagehead h1{margin:0;font-size:1.55rem;line-height:1.1;color:var(--c_ttl);}"
  ".ts-pagehead p{margin:8px 0 0;font-size:.93rem;opacity:.78;max-width:760px;}"
  ".ts-stack{display:grid;gap:16px;}"
  ".ts-note{font-size:.9rem;opacity:.78;line-height:1.45;}"
  ".ts-actions-grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));}"
  ".ts-scanlist>div{padding:10px 0;border-top:1px solid rgba(255,255,255,.07);}"
  ".ts-scanlist>div:first-child{padding-top:0;border-top:0;}"
  ".ts-soft{opacity:.72;}"
  ".ts-center{text-align:center;}"
  ".ts-danger{color:var(--c_txtwrn);font-weight:700;}"
  ".ts-success{color:var(--c_txtscc);font-weight:700;}"
  ".ts-card{"
    "margin:0 0 16px;"
    "padding:16px 18px;"
    "border-radius:20px;"
    "background:linear-gradient(180deg,rgba(255,255,255,.035),rgba(0,0,0,.06)),var(--c_frm);"
    "border:1px solid rgba(255,255,255,.08);"
    "box-shadow:0 12px 30px rgba(0,0,0,.18);"
  "}"
  ".ts-card-head{margin:0 0 12px;}"
  ".ts-card-head h2{margin:0;font-size:1.08rem;line-height:1.2;color:var(--c_ttl);}"
  ".ts-card-head p{margin:6px 0 0;font-size:.9rem;opacity:.74;}"
  ".ts-card-body{}"
".ts-formset{"
  "margin:0 0 16px;"
  "padding:16px 18px 18px;"
  "border-radius:20px;"
  "background:linear-gradient(180deg,rgba(255,255,255,.035),rgba(0,0,0,.06)),var(--c_frm);"
  "border:1px solid rgba(255,255,255,.08);"
  "box-shadow:0 12px 30px rgba(0,0,0,.18);"
"}"
".ts-formset legend{padding:0 8px;color:var(--c_ttl);font-weight:700;}"
".ts-form{display:grid;gap:14px;}"
".ts-field{display:grid;gap:6px;}"
".ts-field label{color:var(--c_ttl);font-weight:700;}"
".ts-field-note{font-size:.9rem;opacity:.72;font-weight:400;}"
".ts-inline-check{display:inline-flex;align-items:center;gap:8px;}"
".ts-inline-check input[type=checkbox]{margin:0;width:1rem;}"
".ts-check-row{display:grid;gap:10px;}"
".ts-check-item{display:inline-flex;align-items:center;gap:8px;font-weight:700;color:var(--c_ttl);}"
".ts-check-item input[type=checkbox]{margin:0;width:1rem;}"
".ts-form-actions{padding-top:4px;}"
".ts-form-actions button{margin:0;}";

const char HTTP_HEAD_STYLE3[] PROGMEM =
  "</style>"
  "</head>"
  "<body>"
  "<div class='ts-shell'>";

const char HTTP_HEAD_STYLE_WIFI[] PROGMEM =
  ".wifi{width:18px;height:12px;position:relative}"
  ".arc{padding:0;position:absolute;border:2px solid transparent;border-radius:50%;border-top-color:var(--c_txt)}"
  ".a0{width:2px;height:3px;top:9px;left:8px}"
  ".a1{width:6px;height:6px;top:6px;left:6px}"
  ".a2{width:12px;height:12px;top:3px;left:3px}"
  ".a3{width:18px;height:18px;top:0px;left:0px}"
  ".o30{opacity:.3}"
  ;

const char HTTP_FORM_UPG_CARD[] PROGMEM =
  "<form method='get' action='u1'>"
  "<p><b>" D_OTA_URL "</b><br><input id='o' placeholder=\"OTA_URL\" value=\"%s\"></p>"
  "<button type='submit'>" D_START_UPGRADE "</button>"
  "</form>";

const char HTTP_FORM_RST_UPG_CARD[] PROGMEM =
  "<form method='post' action='u2?fsz=' enctype='multipart/form-data'>"
  "<input type='file' name='u2'><br><br>"
  "<button type='submit' "
  "onclick='"
    "eb(\"f1\").style.display=\"none\";"
    "eb(\"f2\").style.display=\"block\";"
    "this.form.action+=this.form[\"u2\"].files[0].size;"
    "this.form.submit();"
  "'>%s</button></form>"
  "</div>"
  "<div id='f2' style='display:none;text-align:center;'><b>" D_UPLOAD_STARTED "...</b></div>";

const char HTTP_FORM_RST_UPG_FCT_CARD[] PROGMEM =
  "<form method='post' action='u2?fsz=' enctype='multipart/form-data'>"
  "<input type='file' name='u2'><br><br>"
  "<button type='submit' "
  "onclick='"
    "eb(\"f1\").style.display=\"none\";"
    "var fs=this.form[\"u2\"].files[0].size;"
    "eb((fs>900000)?\"f3\":\"f2\").style.display=\"block\";"
    "this.form.action+=fs;"
    "return upl(this);"
  "'>%s</button></form>"
  "</div>"
  "<div id='f3' style='display:none;text-align:center;'><b>" D_UPLOAD_FACTORY "...</b></div>"
  "<div id='f2' style='display:none;text-align:center;'><b>" D_UPLOAD_STARTED "...</b></div>";

#if defined(USE_ZIGBEE) || defined(USE_LORAWAN_BRIDGE)
// Styles used for Zigbee and LoRaWan Web UI
// Battery icon from https://css.gg/battery
//
  #ifdef USE_UNISHOX_COMPRESSION
    #include "./html_compressed/HTTP_HEAD_STYLE_ZIGBEE.h"
  #else
    #include "./html_uncompressed/HTTP_HEAD_STYLE_ZIGBEE.h"
  #endif
#endif // USE_ZIGBEE

const char HTTP_HEAD_STYLE_SSI[] PROGMEM =
  ".si{display:inline-flex;align-items:flex-end;height:15px;padding:0;}"
  ".si i{width:3px;margin-right:1px;border-radius:3px;background-color:var(--c_txt);}"
  ".si .b0{height:25%}"
  ".si .b1{height:50%}"
  ".si .b2{height:75%}"
  ".si .b3{height:100%}"
  ".o30{opacity:.3}";

const char HTTP_MSG_SLIDER_SHUTTER[] PROGMEM =
  "<td style='width:70%%'>"
  "<div style='padding:0px 2px;text-align:center;font-size:12px;'><span>%s</span>"
  "<input id='s27%d' type='range' min='0' max='100' value='%d' onchange='lc(\"u\",%d,value)'>"
  "</div>"
  "</td>";

const char HTTP_MSG_SLIDER_GRADIENT[] PROGMEM =
  "<td colspan='%d' style='width:%d%%'>"
  "<div id='%s' class='r' style='background-image:linear-gradient(to right,%s,%s);'>"
  "<input id='sl%d' type='range' min='%d' max='%d' value='%d' onchange='lc(\"%c\",%d,value)'>"
  "</div>"
  "</td>";

// https://stackoverflow.com/questions/4057236/how-to-add-onload-event-to-a-div-element
const char HTTP_MSG_EXEC_JAVASCRIPT[] PROGMEM =
  "<img style='display:none;' src onerror=\"";

const char HTTP_MSG_RSTRT[] PROGMEM =
  "<br><div style='text-align:center;'>" D_DEVICE_WILL_RESTART "</div><br>";

const char HTTP_FORM_LOGIN[] PROGMEM =
  "<fieldset class='ts-formset'>"
    "<form method='post' action='/' class='ts-form'>"
      "<div class='ts-field'>"
        "<label>" D_USER "</label>"
        "<input name='USER1' placeholder='" D_USER "'>"
      "</div>"
      "<div class='ts-field'>"
        "<label>" D_PASSWORD "</label>"
        "<input name='PASS1' type='password' placeholder='" D_PASSWORD "'>"
      "</div>"
      "<div class='ts-form-actions'>"
        "<button>" D_OK "</button>"
      "</div>"
    "</form>"
  "</fieldset>";

const char HTTP_FIELDSET_LEGEND[] PROGMEM =
  "<fieldset class='ts-formset'><legend>%s</legend>";

const char HTTP_FORM_GET_ACTION[] PROGMEM =
  "<form method='get' action='%s' class='ts-form'>";

const char HTTP_FORM_BUTTON[] PROGMEM =
  "<form method='get' action='%s' class='ts-form'><div class='ts-form-actions'><button>%s</button></div></form>";


const char HTTP_FORM_WIFI_PART1[] PROGMEM =
  "<div class='ts-field'>"
    "<label>" D_AP1_SSID "%s</label>"
    "<input id='s1' placeholder=\"" D_AP1_SSID_HELP "\" value=\"%s\">"
  "</div>"
  "<div class='ts-field'>"
    "<label class='ts-inline-check'>"
      "<span>" D_AP_PASSWORD "</span>"
      "<input type='checkbox' onclick='sp(\"p1\")'>"
    "</label>"
    "<input id='p1' type='password' placeholder=\"" D_AP_PASSWORD_HELP "\"";

const char HTTP_FORM_WIFI_PART2[] PROGMEM =
  " value=\"" D_ASTERISK_PWD "\">"
  "</div>"
  "<div class='ts-field'>"
    "<label>" D_AP2_SSID " (" STA_SSID2 ")</label>"
    "<input id='s2' placeholder=\"" D_AP2_SSID_HELP "\" value=\"%s\">"
  "</div>"
  "<div class='ts-field'>"
    "<label class='ts-inline-check'>"
      "<span>" D_AP_PASSWORD "</span>"
      "<input type='checkbox' onclick='sp(\"p2\")'>"
    "</label>"
    "<input id='p2' type='password' placeholder=\"" D_AP_PASSWORD_HELP "\" value=\"" D_ASTERISK_PWD "\">"
  "</div>"
  "<div class='ts-field'>"
    "<label>" D_HOSTNAME " <span class='ts-field-note'>(%s)</span></label>"
    "<input id='h' placeholder=\"%s\" value=\"%s\">"
  "</div>"
#ifdef USE_CORS
  "<div class='ts-field'>"
    "<label>" D_CORS_DOMAIN "</label>"
    "<input id='c' placeholder=\"" CORS_DOMAIN "\" value=\"%s\">"
  "</div>"
#endif
  ;

const char HTTP_FORM_LOG[] PROGMEM =
  "<div class='ts-field'>"
    "<label>" D_SYSLOG_HOST " <span class='ts-field-note'>(" SYS_LOG_HOST ")</span></label>"
    "<input id='lh' placeholder=\"" SYS_LOG_HOST "\" value=\"%s\">"
  "</div>"
  "<div class='ts-field'>"
    "<label>" D_SYSLOG_PORT " <span class='ts-field-note'>(" STR(SYS_LOG_PORT) ")</span></label>"
    "<input id='lp' placeholder='" STR(SYS_LOG_PORT) "' value='%d'>"
  "</div>"
  "<div class='ts-field'>"
    "<label>" D_TELEMETRY_PERIOD " <span class='ts-field-note'>(" STR(TELE_PERIOD) ")</span></label>"
    "<input id='lt' placeholder='" STR(TELE_PERIOD) "' value='%d'>"
  "</div>";

const char HTTP_FORM_OTHER[] PROGMEM =
  "<div class='ts-field'>"
    "<label class='ts-inline-check'>"
      "<span>" D_WEB_ADMIN_PASSWORD "</span>"
      "<input type='checkbox' onclick='sp(\"wp\")'>"
    "</label>"
    "<input id='wp' type='password' placeholder=\"" D_WEB_ADMIN_PASSWORD "\" value=\"" D_ASTERISK_PWD "\">"
  "</div>"
  "<div class='ts-check-row'>"
    "<label class='ts-check-item'><input id='b3' type='checkbox'%s><span>" D_HTTP_API_ENABLE "</span></label>"
    "<label class='ts-check-item'><input id='b1' type='checkbox'%s><span>" D_MQTT_ENABLE "</span></label>"
  "</div>"
  "<div class='ts-field'>"
    "<label>" D_DEVICE_NAME " <span class='ts-field-note'>(%s)</span></label>"
    "<input id='dn' placeholder=\"\" value=\"%s\">"
  "</div>";

const char HTTP_FORM_END[] PROGMEM =
  "<div class='ts-form-actions'>"
    "<button name='save' type='submit' class='button bgrn'>" D_SAVE "</button>"
  "</div>"
  "</form></fieldset>";

const char HTTP_DIV_F1_BLOCK[] PROGMEM =
  "<div id='f1' name='f1' class='ts-field' style='display:block;'>";

const char HTTP_CMND_STYLE[] PROGMEM =
  ".ts-console-wrap{display:flex;flex-direction:column;gap:12px;height:calc(100vh - 220px);min-height:calc(100vh - 220px);}"
  ".ts-console-wrap .ts-card-body{display:flex;flex-direction:column;flex:1 1 auto;min-height:0;}"
  ".ts-console-wrap textarea{resize:none;flex:1 1 auto;min-height:0;width:100%;margin:0;}"
  ".ts-console-wrap form{margin:0;}"
  ".ts-console-wrap input{width:100%;}";

const char HTTP_FORM_CMND[] PROGMEM =
  "<section class='ts-card ts-console-wrap'>"
    "<div class='ts-card-head'><h2>" D_CONSOLE "</h2></div>"
    "<div class='ts-card-body'>"
      "<textarea readonly id='t1' wrap='off'></textarea>"
      "<form method='get' onsubmit='return l(1);'>"
        "<input id='c1' placeholder='" D_ENTER_COMMAND "' autofocus>"
      "</form>"
    "</div>"
  "</section>";

const char HTTP_TABLE100[] PROGMEM =
  "<table style='width:100%%'>";

const char HTTP_COUNTER[] PROGMEM =
  "<br><div id='t' style='text-align:center;'></div>";

const char HTTP_END[] PROGMEM =
  "<p></p><div style='text-align:right;font-size:11px;'><hr>Tasmota %s %s " D_BY " Theo Arends</div>"
  "</main>"
  "</div>"
  "</body>"
  "</html>";

const char HTTP_DEVICE_CONTROL[] PROGMEM = "<td style='width:%d%%'><button id='o%d' onclick='la(\"&o=%d\");'>%s%s</button></td>";  // ?o is related to WebGetArg(PSTR("o"), tmp, sizeof(tmp))
const char HTTP_DEVICE_STATE[] PROGMEM = "<td style='width:%d%%;text-align:center;font-weight:%s;font-size:%dpx'>%s</td>";

const char HTTP_STATUS_STICKER[] PROGMEM =
  "<span style='"
  "margin:2px;"
  "cursor:default;"
  "padding:1px 2px;"
  "border-color:#%06x;border-radius:5px;border-style:solid;border-width:1px;"
  "' "
  "%s"    // optional 'title' attributes
  ">"
  "%s"
  "</span>";

const char HTTP_FORM_RESTORE_CARD[] PROGMEM =
  "<form method='post' action='u2?fsz=' enctype='multipart/form-data'>"
  "<input type='file' name='u2'><br><br>"
  "<button type='submit' "
  "onclick='"
    "eb(\"f1\").style.display=\"none\";"
    "eb(\"f2\").style.display=\"block\";"
    "this.form.action+=this.form[\"u2\"].files[0].size;"
    "this.form.submit();"
  "'>%s</button></form>"
  "</div>"
  "<div id='f2' style='display:none;text-align:center;'><b>" D_UPLOAD_STARTED "...</b></div>";

enum ButtonTitle {
  BUTTON_RESTART, BUTTON_RESET_CONFIGURATION,
  BUTTON_MAIN, BUTTON_CONFIGURATION, BUTTON_INFORMATION, BUTTON_FIRMWARE_UPGRADE, BUTTON_MANAGEMENT,
  BUTTON_MODULE, BUTTON_WIFI, BUTTON_LOGGING, BUTTON_OTHER, BUTTON_BACKUP, BUTTON_RESTORE,
  BUTTON_CONSOLE };
const char kButtonTitle[] PROGMEM =
  D_RESTART "|" D_RESET_CONFIGURATION "|"
  D_MAIN_MENU "|" D_CONFIGURATION "|" D_INFORMATION "|" D_FIRMWARE_UPGRADE "|" D_MANAGEMENT "|"
  D_CONFIGURE_MODULE "|" D_CONFIGURE_WIFI "|" D_CONFIGURE_LOGGING "|" D_CONFIGURE_OTHER "|" D_BACKUP_CONFIGURATION "|" D_RESTORE_CONFIGURATION "|"
  D_CONSOLE;
const char kButtonAction[] PROGMEM =
  ".|rt|"
  ".|cn|in|up|mn|"
  "md|wi|lg|co|dl|rs|"
  "cs";
const char kButtonConfirm[] PROGMEM = D_CONFIRM_RESTART "|" D_CONFIRM_RESET_CONFIGURATION;

enum CTypes { CT_HTML, CT_PLAIN, CT_XML, CT_STREAM, CT_APP_JSON, CT_APP_STREAM };
const char kContentTypes[] PROGMEM = "text/html|text/plain|text/xml|text/event-stream|application/json|application/octet-stream";

const char kLoggingOptions[] PROGMEM = D_SERIAL_LOG_LEVEL "|" D_WEB_LOG_LEVEL "|" D_MQTT_LOG_LEVEL "|" D_SYS_LOG_LEVEL;
const char kLoggingLevels[] PROGMEM = D_NONE "|" D_ERROR "|" D_INFO "|" D_DEBUG "|" D_MORE_DEBUG;

const char kEmulationOptions[] PROGMEM = D_NONE "|" D_BELKIN_WEMO "|" D_HUE_BRIDGE;

const char kUploadErrors[] PROGMEM =
//  D_UPLOAD_ERR_1 "|" D_UPLOAD_ERR_2 "|" D_UPLOAD_ERR_3 "|" D_UPLOAD_ERR_4 "|" D_UPLOAD_ERR_5 "|" D_UPLOAD_ERR_6 "|" D_UPLOAD_ERR_7 "|" D_UPLOAD_ERR_8 "|" D_UPLOAD_ERR_9;
  D_UPLOAD_ERR_1 "|" D_UPLOAD_ERR_2 "|" D_UPLOAD_ERR_3 "|" D_UPLOAD_ERR_4 "| |" D_UPLOAD_ERR_6 "|" D_UPLOAD_ERR_7 "|" D_UPLOAD_ERR_8 "|" D_UPLOAD_ERR_9;

const uint16_t DNS_PORT = 53;
enum HttpOptions { HTTP_OFF, HTTP_USER, HTTP_ADMIN, HTTP_MANAGER, HTTP_MANAGER_RESET_ONLY };
enum WebCmndStatus { WEBCMND_DONE, WEBCMND_WRONG_PARAMETERS, WEBCMND_CONNECT_FAILED, WEBCMND_HOST_NOT_FOUND, WEBCMND_MEMORY_ERROR, WEBCMND_VALID_RESPONSE
#ifdef USE_WEBGETCONFIG
  ,WEBCMND_FILE_NOT_FOUND, WEBCMND_OTHER_HTTP_ERROR, WEBCMND_CONNECTION_LOST, WEBCMND_INVALID_FILE
#endif // USE_WEBGETCONFIG
                   };

// NEW ENUMS

enum WSScriptFlags : uint16_t {
  WS_SCRIPT_NONE          = 0,
  WS_SCRIPT_COUNTER       = 1 << 0,
  WS_SCRIPT_ROOT          = 1 << 1,
  WS_SCRIPT_ROOT_AUTOLOAD = 1 << 2,
  WS_SCRIPT_WIFI          = 1 << 3,
  WS_SCRIPT_HIDE          = 1 << 4,
  WS_SCRIPT_CONSOLE       = 1 << 5,
  WS_SCRIPT_RELOAD        = 1 << 6,
  WS_SCRIPT_UPLOAD        = 1 << 7
};

static const char WS_SECTION_TOPBAR_STATUS[] = "ts-topbar-status";
static const char WS_SECTION_ROOT_LIVE[]     = "ts-root-live";
static const char WS_SECTION_SENSOR_LIVE[]   = "ts-sensor-live";
static const char WS_SECTION_JS[]            = "@js";

static const char WS_RF_BEGIN[] = "~#RF#~";
static const char WS_RF_MID[]   = "~#RM#~";
static const char WS_RF_END[]   = "~#RE#~";

void WSContentSendToolbarStatusInner(void);
void WSContentSendRootLive(void);
void WSContentSendSensorLive(void);

typedef void (*WSRefreshRenderer)(void);

struct WSRefreshSection {
  const char *id;
  WSRefreshRenderer render;
};


DNSServer *DnsServer;
ESP8266WebServer *Webserver;

struct WEB {
  String chunk_buffer = "";                         // Could be max 2 * CHUNKED_BUFFER_SIZE
  uint32_t upload_size = 0;
  uint32_t light_shutter_button_mask;
  uint32_t buttons_non_light_non_shutter;
  uint32_t slider_update_time = 0;
  int slider[LST_MAX];
  int8_t shutter_slider[16];                        // MAX_SHUTTERS_ESP32
  uint16_t upload_error = 0;
  uint8_t state = HTTP_OFF;
  uint8_t upload_file_type;
  uint8_t config_block_count = 0;
  bool upload_services_stopped = false;
  bool reset_web_log_flag = false;                  // Reset web console log
  bool initial_config = false;
  bool cflg;
} Web;

/*********************************************************************************************/

// Helper function to avoid code duplication (saves 4k Flash)
// arg can be in PROGMEM
static void WebGetArg(const char* arg, char* out, size_t max) {
  String s = Webserver->arg((const __FlashStringHelper *)arg);
  strlcpy(out, s.c_str(), max);
//  out[max-1] = '\0';  // Ensure terminating NUL
}

/*-------------------------------------------------------------------------------------------*/

String AddWebCommand(const char* command, const char* arg, const char* dflt) {
/*
  // OK but fixed max argument
  char param[200];                             // Allow parameter with lenght up to 199 characters
  WebGetArg(arg, param, sizeof(param));
  uint32_t len = strlen(param);
  char cmnd[232];
  snprintf_P(cmnd, sizeof(cmnd), PSTR(";%s %s"), command, (0 == len) ? dflt : (StrCaseStr_P(command, PSTR("Password")) && (len < 5)) ? "" : param);
  return String(cmnd);
*/
/*
  // Any argument size (within stack space) +48 bytes
  String param = Webserver->arg((const __FlashStringHelper *)arg);
  uint32_t len = param.length();
//  char cmnd[len + strlen_P(command) + strlen_P(dflt) + 4];
  char cmnd[64 + len];
  snprintf_P(cmnd, sizeof(cmnd), PSTR(";%s %s"), command, (0 == len) ? dflt : (StrCaseStr_P(command, PSTR("Password")) && (len < 5)) ? "" : param.c_str());
  return String(cmnd);
*/
  // Any argument size (within heap space) +24 bytes
  // Exception (3) if not first moved from flash to stack
  // Exception (3) if not using __FlashStringHelper
  // Exception (3) if not FPSTR()
//  char rcommand[strlen_P(command) +1];
//  snprintf_P(rcommand, sizeof(rcommand), command);
//  char rdflt[strlen_P(dflt) +1];
//  snprintf_P(rdflt, sizeof(rdflt), dflt);
  String result = F(";");
//  result += rcommand;
//  result += (const __FlashStringHelper *)command;
  result += FPSTR(command);
  result += F(" ");
  String param = Webserver->arg(FPSTR(arg));
  uint32_t len = param.length();
  if (0 == len) {
//    result += rdflt;
//    result += (const __FlashStringHelper *)dflt;
    result += FPSTR(dflt);
  }
  else if (!(StrCaseStr_P(command, PSTR("Password")) && (len < 5))) {
    result += param;
  }
  return result;
}

/*-------------------------------------------------------------------------------------------*/

static bool WifiIsInManagerMode(void) {
  return (HTTP_MANAGER == Web.state || HTTP_MANAGER_RESET_ONLY == Web.state);
}

/*-------------------------------------------------------------------------------------------*/

void ShowWebSource(uint32_t source) {
  if ((source > 0) && (source < SRC_MAX)) {
    char stemp1[20];
    AddLog(LOG_LEVEL_DEBUG, PSTR("SRC: %s from %s"), GetTextIndexed(stemp1, sizeof(stemp1), source, kCommandSource), Webserver->client().remoteIP().toString().c_str());
  }
}

/*-------------------------------------------------------------------------------------------*/

void ExecuteWebCommand(char* svalue, uint32_t source = SRC_WEBGUI);
void ExecuteWebCommand(char* svalue, uint32_t source) {
  ShowWebSource(source);
  TasmotaGlobal.last_source = source;
  ExecuteCommand(svalue, SRC_IGNORE);
}

/*-------------------------------------------------------------------------------------------*/

// replace the series of `Webserver->on()` with a table in PROGMEM
typedef struct WebServerDispatch_t {
  char uri[3];   // the prefix "/" is added automatically
  uint8_t method;
  void (*handler)(void);
} WebServerDispatch_t;

const WebServerDispatch_t WebServerDispatch[] PROGMEM = {
  { "",   HTTP_ANY, HandleRoot },
  { "up", HTTP_ANY, HandleUpgradeFirmware },
  { "u1", HTTP_ANY, HandleUpgradeFirmwareStart },   // OTA
  { "u2", HTTP_OPTIONS, HandlePreflightRequest },
  { "u3", HTTP_ANY, HandleUploadDone },
  { "u4", HTTP_GET, HandleSwitchBootPartition },
  { "mn", HTTP_GET, HandleManagement },
  { "cs", HTTP_GET, HandleConsole },
  { "cs", HTTP_OPTIONS, HandlePreflightRequest },
  { "cm", HTTP_ANY, HandleHttpCommand },
  { "cn", HTTP_ANY, HandleConfiguration },
  { "md", HTTP_ANY, HandleModuleConfiguration },
  { "wi", HTTP_ANY, HandleWifiConfiguration },
  { "lg", HTTP_ANY, HandleLoggingConfiguration },
  { "co", HTTP_ANY, HandleOtherConfiguration },
  { "dl", HTTP_ANY, HandleBackupConfiguration },
  { "rs", HTTP_ANY, HandleRestoreConfiguration },
  { "rt", HTTP_ANY, HandleResetConfiguration },
  { "in", HTTP_ANY, HandleInformation },
  { "if", HTTP_ANY, HandleInformationDevice },
  { "is", HTTP_ANY, HandleInformationSensors }
};

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void WebServer_on(const char * prefix, void (*func)(void), uint8_t method = HTTP_ANY) {
  if (Webserver == nullptr) { return; }


  Webserver->on(prefix, (HTTPMethod) method, func);

}


void WebServer_removeRoute(const char * prefix, uint8_t method = HTTP_ANY) {
  if (Webserver == nullptr) { return; }
  Webserver->removeRoute(prefix, (HTTPMethod) method);
}


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

// Always listens to all interfaces, so we don't need an IP address anymore
void StartWebserver(int type) {
  if (!Settings->web_refresh) { Settings->web_refresh = HTTP_REFRESH_TIME; }
  if (!Web.state) {

    for (uint32_t i = 0; i < LST_MAX; i++) {
      Web.slider[i] = -1;
    }
    for (uint32_t i = 0; i < sizeof(Web.shutter_slider); i++) {
      Web.shutter_slider[i] = -1;
    }

    if (!Webserver) {
      Webserver = new ESP8266WebServer((HTTP_MANAGER == type || HTTP_MANAGER_RESET_ONLY == type) ? 80 : WEB_PORT);

      const char* headerkeys[] = { "Referer", "Host" };
      size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
      Webserver->collectHeaders(headerkeys, headerkeyssize);

      // call `Webserver->on()` on each entry
      for (uint32_t i=0; i<nitems(WebServerDispatch); i++) {
        const WebServerDispatch_t & line = WebServerDispatch[i];
        // copy uri in RAM and prefix with '/'
        char uri[4];
        uri[0] = '/';
        uri[1] = pgm_read_byte(&line.uri[0]);
        uri[2] = pgm_read_byte(&line.uri[1]);
        uri[3] = '\0';
        // register
        WebServer_on(uri, line.handler, pgm_read_byte(&line.method));
      }
      Webserver->onNotFound(HandleNotFound);
//      Webserver->on(F("/u2"), HTTP_POST, HandleUploadDone, HandleUploadLoop);  // this call requires 2 functions so we keep a direct call
      Webserver->on("/u2", HTTP_POST, HandleUploadDone, HandleUploadLoop);  // this call requires 2 functions so we keep a direct call

      XdrvXsnsCall(FUNC_WEB_ADD_HANDLER);

      if (!Web.initial_config) {
        Web.initial_config = (!strlen(SettingsText(SET_STASSID1)) && !strlen(SettingsText(SET_STASSID2)));
        if (Web.initial_config) { AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP "Blank Device - Initial Configuration")); }
      }
    }
    Web.reset_web_log_flag = false;

    Webserver->begin(); // Web server start
  }
  if (Web.state != type) {
    AddLogServerActive(PSTR(D_LOG_HTTP "Web"));
    TasmotaGlobal.rules_flag.http_init = 1;
    Web.state = type;
  }
}

/*-------------------------------------------------------------------------------------------*/

void StopWebserver(void) {
  if (Web.state) {
    Webserver->close();
    Web.state = HTTP_OFF;
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_HTTP D_WEBSERVER_STOPPED));
  }
}

/*-------------------------------------------------------------------------------------------*/

void WifiManagerBegin(bool reset_only) {
  // setup AP
  if (!Web.initial_config) { AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_WCFG_2_WIFIMANAGER " " D_ACTIVE_FOR_3_MINUTES)); }
  if (!TasmotaGlobal.global_state.wifi_down) {
    WifiSetMode(WIFI_AP_STA);
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_WIFIMANAGER_SET_ACCESSPOINT_AND_STATION));
  } else {
    WifiSetMode(WIFI_AP);
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_WIFIMANAGER_SET_ACCESSPOINT));
  }

  //StopWebserver();

  DnsServer = new DNSServer();

  int channel = WIFI_SOFT_AP_CHANNEL;
  if ((channel < 1) || (channel > 13)) { channel = 1; }

  // bool softAP(const char* ssid, const char* passphrase = NULL, int channel = 1, int ssid_hidden = 0, int max_connection = 4);
  WiFi.softAP(TasmotaGlobal.hostname, WIFI_AP_PASSPHRASE, channel, 0, 1);
  delay(500); // Without delay I've seen the IP address blank
  /* Setup the DNS server redirecting all the domains to the apIP */
  DnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  DnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

  StartWebserver((reset_only ? HTTP_MANAGER_RESET_ONLY : HTTP_MANAGER));
}

/*-------------------------------------------------------------------------------------------*/

void PollDnsWebserver(void) {
  if (DnsServer) { DnsServer->processNextRequest(); }
  if (Webserver) { Webserver->handleClient(); }
}

/*********************************************************************************************/

bool WebAuthenticate(void) {
  if (strlen(SettingsText(SET_WEBPWD)) && (HTTP_MANAGER_RESET_ONLY != Web.state)) {
    return Webserver->authenticate(WEB_USERNAME, SettingsText(SET_WEBPWD));
  } else {
    return true;
  }
}

/*-------------------------------------------------------------------------------------------*/

bool HttpCheckPriviledgedAccess(bool autorequestauth = true) {
  if (HTTP_USER == Web.state) {
    HandleRoot();
    return false;
  }
  if (autorequestauth && !WebAuthenticate()) {
    Webserver->requestAuthentication();
    return false;
  }

  if (!Settings->flag5.disable_referer_chk && !WifiIsInManagerMode()) {
    String referer = Webserver->header(F("Referer"));  // http://demo/? or http://192.168.2.153/?
    if (referer.length()) {
      referer.toUpperCase();
      String hostname = TasmotaGlobal.hostname;
      hostname.toUpperCase();
      // TODO rework if IPv6
      if ((referer.indexOf(hostname) == 7) || (referer.indexOf(WiFi.localIP().toString()) == 7)) {
        return true;
      }

#if defined(USE_ETHERNET)
      hostname = EthernetHostname();
      hostname.toUpperCase();
      // TODO rework if IPv6
      if ((referer.indexOf(hostname) == 7) || (referer.indexOf(EthernetLocalIP().toString()) == 7)) {
        return true;
      }
#endif  // USE_ETHERNET
    }
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_HTTP "Referer '%s' denied. Use 'SO128 1' for HTTP API commands. 'Webpassword' is recommended."), referer.c_str());
    return false;
  } else {
#if defined(USE_MI_ESP32) && !defined(USE_BLE_ESP32)
    MI32suspendScanTask();
#endif // defined(USE_MI_ESP32) && !defined(USE_BLE_ESP32)
    return true;
  }
}

#ifdef USE_CORS
/*-------------------------------------------------------------------------------------------*/

void HttpHeaderCors(void) {
  if (strlen(SettingsText(SET_CORS))) {
    Webserver->sendHeader(F("Access-Control-Allow-Origin"), SettingsText(SET_CORS));
  }
}
#endif
/*********************************************************************************************\
 * NEW HELPER
\*********************************************************************************************/
void WSScriptStart(void) {
  WSContentSend_P(PSTR("<script>"));
}

void WSScriptStop(void) {
  WSContentSend_P(PSTR("</script>"));
}

void WSSendPageScripts(uint16_t flags, uint32_t reload_time = 0) {
  if (!flags) { return; }

  WSScriptStart();

  if ((flags & WS_SCRIPT_COUNTER) && WifiIsInManagerMode() && (!Web.initial_config)) {
    if (WifiConfigCounter()) {
      WSContentSendRaw_P(HTTP_SCRIPT_COUNTER);
    }
  }

  if (flags & WS_SCRIPT_ROOT) {
    WSContentSend_P(HTTP_SCRIPT_ROOT, Settings->web_refresh);
    WSContentSendRaw_P(HTTP_SCRIPT_ROOT_PART2);
  }

  if (flags & WS_SCRIPT_ROOT_AUTOLOAD) {
    WSContentSendRaw_P(HTTP_SCRIPT_ROOT_AUTOLOAD);
  }

  if (flags & WS_SCRIPT_WIFI) {
    WSContentSendRaw_P(HTTP_SCRIPT_WIFI);
  }

  if (flags & WS_SCRIPT_HIDE) {
    WSContentSendRaw_P(HTTP_SCRIPT_HIDE);
  }

  if (flags & WS_SCRIPT_CONSOLE) {
    WSContentSend_P(HTTP_SCRIPT_CONSOL, Settings->web_refresh);
  }

  if ((flags & WS_SCRIPT_RELOAD) && reload_time) {
    WSContentSend_P(HTTP_SCRIPT_RELOAD_TIME, reload_time);
  }

  if (flags & WS_SCRIPT_UPLOAD) {
    WSContentSendRaw_P(HTTP_SCRIPT_UPLOAD);
  }

  WSScriptStop();
}

void WSContentRefreshSectionBegin(const char* id, const char* css_class = nullptr) {
  if ((nullptr != css_class) && css_class[0]) {
    WSContentSend_P(PSTR("<div id='%s' data-tsrf='1' class='%s'>"), id, css_class);
  } else {
    WSContentSend_P(PSTR("<div id='%s' data-tsrf='1'>"), id);
  }
}

void WSContentSectionEnd(void) {
  WSContentSend_P(PSTR("</div>"));
}

bool WSRefreshListContains(const char *list, const char *id) {
  if (!list || !*list || !id || !*id) { return false; }

  size_t id_len = strlen(id);
  const char *p = list;

  while (*p) {
    while ((*p == ',') || (*p == ' ')) { p++; }
    const char *start = p;

    while (*p && (*p != ',')) { p++; }
    size_t len = p - start;

    while (len && (start[len - 1] == ' ')) { len--; }

    if ((len == id_len) && (0 == strncmp(start, id, id_len))) {
      return true;
    }
  }
  return false;
}

void WSSendRefreshFragmentBegin(const char *id) {
  WSContentSeparator(3);
  WSContentSend_P(PSTR("%s%s%s"), WS_RF_BEGIN, id, WS_RF_MID);
}

void WSSendRefreshFragmentEnd(void) {
  WSContentSend_P(PSTR("%s"), WS_RF_END);
  WSContentSeparator(3);
}

void WSContentSendToolbarStatusInner(void) {
  if (WifiIsInManagerMode()) {
    WSContentSend_P(PSTR("<span class='ts-status'>AP</span>"));
  }

#ifdef USE_WEB_STATUS_LINE
#ifdef USE_WEB_STATUS_LINE_WIFI
  if (Settings->flag4.network_wifi) {
    int32_t rssi = WiFi.RSSI();
    WSContentSend_P(
      PSTR("<span class='ts-status' title='%s: " D_RSSI " %d%% (%d dBm)'>"
             "<span class='wifi'>"
               "<span class='arc a3%s'></span>"
               "<span class='arc a2%s'></span>"
               "<span class='arc a1%s'></span>"
               "<span class='arc a0'></span>"
             "</span>"
             "<span class='mono'>%d%%</span>"
           "</span>"),
      SettingsTextEscaped(SET_STASSID1 + Settings->sta_active).c_str(),
      WifiGetRssiAsQuality(rssi), rssi,
      rssi < -55 ? " o30" : "",
      rssi < -70 ? " o30" : "",
      rssi < -85 ? " o30" : "",
      WifiGetRssiAsQuality(rssi)
    );
  }
#endif  // USE_WEB_STATUS_LINE_WIFI

#if defined(USE_ETHERNET)
  if (EthernetHasIP()) {
    WSContentSend_P(PSTR("<span class='ts-status'>ETH</span>"));
  }
#endif

  if (Settings->flag.mqtt_enabled) {
    WSContentSend_P(PSTR("<span class='ts-status'>MQTT</span>"));
  }

#ifdef USE_WEB_STATUS_LINE_HEAP
  WSContentSend_P(PSTR("<span class='ts-status'>%ik</span>"), ESP_getFreeHeap() / 1024);
#endif // USE_WEB_STATUS_LINE_HEAP

  if (!WifiIsInManagerMode()) {
    WSContentSend_P(PSTR("<span class='ts-status mono'>%s</span>"), IPGetListeningAddressStr().c_str());
  }

  WSContentSend_P(PSTR("<span class='ts-status'>"));
  XsnsXdrvCall(FUNC_WEB_STATUS_RIGHT);
  WSContentSend_P(PSTR("</span>"));
#else
  if (!WifiIsInManagerMode()) {
    WSContentSend_P(PSTR("<span class='ts-status mono'>%s</span>"), IPGetListeningAddressStr().c_str());
  }
#endif
}

void WSContentSendToolbarStatus(void) {
  WSContentRefreshSectionBegin(WS_SECTION_TOPBAR_STATUS, "ts-topbar-right");
  WSContentSendToolbarStatusInner();
  WSContentSectionEnd();
}

void WSContentSendToolbarMenu(void) {
  WSContentSend_P(PSTR(
  "<nav id='ts-nav' class='ts-menu'>"
  ));

  if (WifiIsInManagerMode()) {
    WSContentSend_P(PSTR(
      "<div class='ts-menu-group'>"
        "<a class='ts-menu-link' href='wi' onclick='tsCloseMenus();'>"
          "<span class='ts-menu-copy'>"
            "<span class='ts-menu-label'>" D_CONFIGURE_WIFI "</span>"
            "<span class='ts-menu-note'>WiFi manager and onboarding</span>"
          "</span>"
        "</a>"
        "<a class='ts-menu-link' href='.' onclick='tsCloseMenus();'>"
          "<span class='ts-menu-copy'>"
            "<span class='ts-menu-label'>" D_MAIN_MENU "</span>"
            "<span class='ts-menu-note'>Back to the root page</span>"
          "</span>"
        "</a>"
      "</div>"
    ));
    WSContentSend_P(PSTR(
      "<div class='ts-menu-group'>"
        "<a class='ts-menu-link' href='rs' onclick='tsCloseMenus();'>"
          "<span class='ts-menu-copy'>"
            "<span class='ts-menu-label'>" D_RESTORE_CONFIGURATION "</span>"
            "<span class='ts-menu-note'>Restore a previously saved configuration</span>"
          "</span>"
        "</a>"
      "</div>"
    ));
    WSContentSend_P(PSTR("</nav>"));
    return;
  }

  WSContentSend_P(PSTR(
    "<div class='ts-menu-group'>"
      "<a class='ts-menu-link' href='.' onclick='tsCloseMenus();'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_MAIN_MENU "</span>"
          "<span class='ts-menu-note'>Dashboard and live status</span>"
        "</span>"
      "</a>"
    "</div>"
  ));

  WSContentSend_P(PSTR(
    "<div class='ts-menu-group'>"
      "<button type='button' class='ts-menu-toggle' onclick='return tsToggleSub(\"ts-sub-cfg\",\"ts-ch-cfg\",event);'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_CONFIGURATION "</span>"
          "<span class='ts-menu-note'>Device settings and runtime options</span>"
        "</span>"
        "<span id='ts-ch-cfg' class='ts-chevron'>&#9656;</span>"
      "</button>"
      "<div id='ts-sub-cfg' class='ts-menu-sub'>"
        "<a class='ts-sub-link' href='cn' onclick='tsCloseMenus();'>" D_CONFIGURATION "</a>"
        "<a class='ts-sub-link' href='md' onclick='tsCloseMenus();'>" D_CONFIGURE_MODULE "</a>"
        "<a class='ts-sub-link' href='wi' onclick='tsCloseMenus();'>" D_CONFIGURE_WIFI "</a>"  ));
if (Settings->flag.mqtt_enabled) {
  WSContentSend_P(PSTR("<a class='ts-sub-link' href='mq' onclick='tsCloseMenus();'>" D_CONFIGURE_MQTT "</a>"  ));
}

  WSContentSend_P(PSTR(
        "<a class='ts-sub-link' href='lg' onclick='tsCloseMenus();'>" D_CONFIGURE_LOGGING "</a>"
        "<a class='ts-sub-link' href='co' onclick='tsCloseMenus();'>" D_CONFIGURE_OTHER "</a>"
      "</div>"
    "</div>"

    "<div class='ts-menu-group'>"
      "<button type='button' class='ts-menu-toggle' onclick='return tsToggleSub(\"ts-sub-info\",\"ts-ch-info\",event);'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_INFORMATION "</span>"
          "<span class='ts-menu-note'>Device details and live sensor view</span>"
        "</span>"
        "<span id='ts-ch-info' class='ts-chevron'>&#9656;</span>"
      "</button>"
      "<div id='ts-sub-info' class='ts-menu-sub'>"
        "<a class='ts-sub-link' href='if' onclick='tsCloseMenus();'>Device Info</a>"
        "<a class='ts-sub-link' href='is' onclick='tsCloseMenus();'>Sensors</a>"
      "</div>"
    "</div>"

    "<div class='ts-menu-group'>"
      "<a class='ts-menu-link' href='up' onclick='tsCloseMenus();'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_FIRMWARE_UPGRADE "</span>"
          "<span class='ts-menu-note'>OTA or local upload</span>"
        "</span>"
      "</a>"
    "</div>"

    "<div class='ts-menu-group'>"
      "<button type='button' class='ts-menu-toggle' onclick='return tsToggleSub(\"ts-sub-maint\",\"ts-ch-maint\",event);'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_MANAGEMENT "</span>"
          "<span class='ts-menu-note'>Console, backup and maintenance actions</span>"
        "</span>"
        "<span id='ts-ch-maint' class='ts-chevron'>&#9656;</span>"
      "</button>"
      "<div id='ts-sub-maint' class='ts-menu-sub'>"
        "<a class='ts-sub-link' href='cs' onclick='tsCloseMenus();'>Tasmota " D_CONSOLE "</a>"
        "<a class='ts-sub-link' href='bc' onclick='tsCloseMenus();'> Berry Console </a>"
        "<a class='ts-sub-link' href='ufsu' onclick='tsCloseMenus();'> Manage File System</a>"
        "<a class='ts-sub-link' href='mn' onclick='tsCloseMenus();'>Other " D_MANAGEMENT "</a>"
        
      "</div>"
    "</div>"
  ));

  WSContentSend_P(PSTR(
    "<div class='ts-menu-group'>"
      "<a class='ts-menu-link' href='/?rst=1' onclick='tsCloseMenus();'>"
        "<span class='ts-menu-copy'>"
          "<span class='ts-menu-label'>" D_RESTART "</span>"
          "<span class='ts-menu-note'>Restart the device</span>"
        "</span>"
      "</a>"
    "</div>"
    "</nav>"
  ));
}

void WSContentSendToolbarShell(void) {
  WSContentSend_P(PSTR("<header class='ts-topbar'>"));

  WSContentSend_P(PSTR("<div class='ts-topbar-left'>"));

  WSContentSend_P(PSTR(
  "<button id='ts-nav-btn' type='button' class='ts-brand' onclick='return tsToggleNav(event);'>System</button>"
  ));

  WSContentSendToolbarMenu();

  WSContentSend_P(PSTR("</div>"));

  WSContentSendToolbarStatus();

  WSContentSend_P(PSTR("</header><main class='ts-content'>"));
}


void WSContentPageHeader(const char* title, const char* subtitle = nullptr) {
  WSContentSend_P(PSTR("<section class='ts-pagehead'><h1>%s</h1>"), title);
  if ((nullptr != subtitle) && subtitle[0]) {
    WSContentSend_P(PSTR("<p>%s</p>"), subtitle);
  }
  WSContentSend_P(PSTR("</section>"));
}

void WSContentCardStart(const char* title, const char* subtitle) {
  WSContentSend_P(PSTR("<section class='ts-card'>"));

  if (((nullptr != title) && title[0]) || ((nullptr != subtitle) && subtitle[0])) {
    WSContentSend_P(PSTR("<div class='ts-card-head'>"));
    if ((nullptr != title) && title[0]) {
      WSContentSend_P(PSTR("<h2>%s</h2>"), title);
    }
    if ((nullptr != subtitle) && subtitle[0]) {
      WSContentSend_P(PSTR("<p>%s</p>"), subtitle);
    }
    WSContentSend_P(PSTR("</div>"));
  }

  WSContentSend_P(PSTR("<div class='ts-card-body'>"));
}

void WSContentCardEnd(void) {
  WSContentSend_P(PSTR("</div></section>"));
}

void WSContentActionsStart(void) {
  WSContentSend_P(PSTR("<div class='ts-actions-grid'>"));
}

void WSContentActionsEnd(void) {
  WSContentSend_P(PSTR("</div>"));
}

void WSContentActionButton(const char* action, const char* label) {
  WSContentSend_P(PSTR("<form method='get' action='%s'><button>%s</button></form>"), action, label);
}

void WSContentFormStart(const char* legend, const char* action) {
  WSContentSend_P(PSTR("<fieldset class='ts-formset'><legend>%s</legend><form method='get' action='%s' class='ts-form'>"), legend, action);
}

void WSContentFormEnd(void) {
  WSContentSend_P(PSTR("<div class='ts-form-actions'><button name='save' type='submit' class='button bgrn'>" D_SAVE "</button></div></form></fieldset>"));
}

/*-------------------------------------------------------------------------------------------*/

void WSHeaderSend(void) {
  char server[32];
  snprintf_P(server, sizeof(server), PSTR("Tasmota/%s (%s)"), TasmotaGlobal.version, GetDeviceHardware().c_str());
  Webserver->sendHeader(F("Server"), server);
  Webserver->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
  Webserver->sendHeader(F("Pragma"), F("no-cache"));
  Webserver->sendHeader(F("Expires"), F("-1"));
#ifdef USE_CORS
  HttpHeaderCors();
#endif
}

/*********************************************************************************************\
 * HTTP Content Page handler
\*********************************************************************************************/

void WSSend(int code, int ctype, const String& content)
{
  char ct[25];  // strlen("application/octet-stream") +1 = Longest Content type string
  Webserver->send(code, GetTextIndexed(ct, sizeof(ct), ctype, kContentTypes), content);
}

/*********************************************************************************************\
 * HTTP Content Chunk handler
\*********************************************************************************************/

void WSContentBegin(int code, int ctype) {
  Webserver->client().flush();
  WSHeaderSend();
  Webserver->setContentLength(CONTENT_LENGTH_UNKNOWN);
  WSSend(code, ctype, "");                         // Signal start of chunked content
  Web.chunk_buffer = "";
}

/*-------------------------------------------------------------------------------------------*/

void _WSContentSend(const char* content, size_t size) {  // Lowest level sendContent for all core versions
  Webserver->sendContent(content, size);

  SHOW_FREE_MEM(PSTR("WSContentSend"));
  DEBUG_CORE_LOG(PSTR("WEB: Chunk size %d"), size);
}

/*-------------------------------------------------------------------------------------------*/

void _WSContentSend(const String& content) {       // Low level sendContent for all core versions
  _WSContentSend(content.c_str(), content.length());
}

/*-------------------------------------------------------------------------------------------*/

void WSContentFlush(void) {
  if (Web.chunk_buffer.length() > 0) {
    _WSContentSend(Web.chunk_buffer);              // Flush chunk buffer
    Web.chunk_buffer = "";
  }
}

/*-------------------------------------------------------------------------------------------*/

void _WSContentSendBufferChunk_P(const char* content) {
  int len = strlen_P(content);
  if (len < CHUNKED_BUFFER_SIZE) {                 // Append chunk buffer with small content
    Web.chunk_buffer += (const __FlashStringHelper *)content;
    len = Web.chunk_buffer.length();
  }
  if (len >= CHUNKED_BUFFER_SIZE) {                // Either content or chunk buffer is oversize
    WSContentFlush();                              // Send chunk buffer before possible content oversize
  }
  len = strlen_P(content);
  if (len >= CHUNKED_BUFFER_SIZE) {                // Content is oversize
    _WSContentSend(content, len);                  // Send content
  }
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSendRaw_P(const char* content) {     // Content sent without formatting
  if (nullptr == content || !strlen_P(content)) { return; }

  WSContentSeparator(2);                           // Print separator on next WSContentSeparator(1)
  _WSContentSendBufferChunk_P(content);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend(const char* content, size_t size) {
  // To speed up transmission use chunked buffer if possible
  if (size < CHUNKED_BUFFER_SIZE) {
    // Terminate non-terminated content
    char buffer[size +1];
    strlcpy(buffer, content, sizeof(buffer));      // Terminate with '\0'
    _WSContentSendBufferChunk_P(buffer);
  } else {
    WSContentFlush();                              // Flush chunk buffer
    _WSContentSend(content, size);
  }
}

/*-------------------------------------------------------------------------------------------*/

void _WSContentSendBuffer(bool decimal, const char * formatP, va_list arg) {
  char* content = ext_vsnprintf_malloc_P(formatP, arg);
  if (content == nullptr) { return; }              // Avoid crash

  int len = strlen(content);
  if (decimal && (D_DECIMAL_SEPARATOR[0] != '.')) {
    for (uint32_t i = 0; i < len; i++) {
      if ('.' == content[i]) {
        content[i] = D_DECIMAL_SEPARATOR[0];
      }
    }
  }

  WSContentSendRaw_P(content);
  free(content);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_P(const char* formatP, ...) {   // Content send snprintf_P char data
  // This uses char strings. Be aware of sending %% if % is needed
  va_list arg;
  va_start(arg, formatP);
  _WSContentSendBuffer(false, formatP, arg);
  va_end(arg);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_PD(const char* formatP, ...) {  // Content send snprintf_P char data checked for decimal separator
  // This uses char strings. Be aware of sending %% if % is needed
  va_list arg;
  va_start(arg, formatP);
  _WSContentSendBuffer(true, formatP, arg);
  va_end(arg);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentStart_P(const char* title, bool auth) {
  if (auth && !WebAuthenticate()) {
    return Webserver->requestAuthentication();
  }

  WSContentBegin(200, CT_HTML);

  if (title != nullptr) {
    WSContentSend_P(HTTP_HEADER1, 
      PSTR(D_HTML_LANGUAGE), 
      SettingsTextEscaped(SET_DEVICENAME).c_str(), 
      title);
  }
}

/*-------------------------------------------------------------------------------------------*/

void WSContentStart_P(const char* title) {
  WSContentStart_P(title, true);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSendStyle_P(const char* formatP, ...) {
  WSScriptStart();

  WSContentSendRaw_P(HTTP_SCRIPT_CORE);

  if (WifiIsInManagerMode() && (!Web.initial_config)) {
    if (WifiConfigCounter()) {
      WSContentSendRaw_P(HTTP_SCRIPT_COUNTER);
    }
  }


  WSScriptStop();

  // Output style root colors by names
  WSContentSend_P(HTTP_HEAD_STYLE_ROOT_COLOR,
                  WebColor(COL_BACKGROUND),           // --c_bg
                  WebColor(COL_FORM),                 // --c_frm
                  WebColor(COL_TITLE),                // --c_ttl
                  WebColor(COL_TEXT),                 // --c_txt
                  WebColor(COL_TEXT_WARNING),         // --c_txtwrn
                  WebColor(COL_TEXT_SUCCESS),         // --c_txtscc
                  WebColor(COL_BUTTON),               // --c_btn
                  WebColor(COL_BUTTON_OFF),           // --c_btnoff
                  WebColor(COL_BUTTON_TEXT),          // --c_btntxt
                  WebColor(COL_BUTTON_HOVER),         // --c_btnhvr
                  WebColor(COL_BUTTON_RESET),         // --c_btnrst
                  WebColor(COL_BUTTON_RESET_HOVER),   // --c_btnrsthvr
                  WebColor(COL_BUTTON_SAVE),          // --c_btnsv
                  WebColor(COL_BUTTON_SAVE_HOVER),    // --c_btnsvhvr
                  WebColor(COL_INPUT),                // --c_in
                  WebColor(COL_INPUT_TEXT),           // --c_intxt
                  WebColor(COL_CONSOLE),              // --c_csl
                  WebColor(COL_CONSOLE_TEXT),         // --c_csltxt
                  WebColor(COL_TIMER_TAB_BACKGROUND), // --c_tab
                  WebColor(COL_TIMER_TAB_TEXT)        // --c_tabtxt
  );

  WSContentSendRaw_P(HTTP_HEAD_STYLE1);
  WSContentSendRaw_P(HTTP_HEAD_STYLE2);
  WSContentSendRaw_P(HTTP_HEAD_STYLE_SHELL);
  WSContentSendRaw_P(HTTP_HEAD_STYLE_SHELL2);

#ifdef USE_WEB_STATUS_LINE_WIFI
  WSContentSendRaw_P(HTTP_HEAD_STYLE_WIFI);
#endif
#if defined(USE_ZIGBEE) || defined(USE_LORAWAN_BRIDGE)
  WSContentSendRaw_P(HTTP_HEAD_STYLE_ZIGBEE);
#endif // USE_ZIGBEE
  if (formatP != nullptr) {
    // This uses char strings. Be aware of sending %% if % is needed
    va_list arg;
    va_start(arg, formatP);
    _WSContentSendBuffer(false, formatP, arg);
    va_end(arg);
  }
  if (strlen(SettingsText(SET_CANVAS))) {
//    WSContentSend_P(PSTR("body{background:%s;background-repeat:no-repeat;background-attachment:fixed;background-size:cover;}"), SettingsText(SET_CANVAS));
    WSContentSend_P(PSTR("body{background:%s 0 0 / cover no-repeat fixed;}"), SettingsText(SET_CANVAS));
  }

  WSContentSend_P(HTTP_HEAD_STYLE3);

  WSContentSendToolbarShell();

  // SetOption53 - Show hostname and IP address in GUI main menu
#if (RESTART_AFTER_INITIAL_WIFI_CONFIG)
  if (Settings->flag3.gui_hostname_ip) {            // SetOption53 - (GUI) Show hostname and IP address in GUI main menu
#else
  if ( Settings->flag3.gui_hostname_ip || ( (WiFi.getMode() == WIFI_AP_STA) && (!Web.initial_config) )  ) {
#endif
    bool lip = WifiHasIP();
    bool sip = (static_cast<uint32_t>(WiFi.softAPIP()) != 0);
    bool eip = false;
    if (lip || sip) {
        WSContentSend_P(PSTR("<div class='ts-hostline'>%s%s (%s%s%s)"),    // tasmota.local (192.168.2.12, 192.168.4.1)
        TasmotaGlobal.hostname,
        (Mdns.begun) ? PSTR(".local") : "",
        (lip) ? WiFi.localIP().toString().c_str() : "",
        (lip && sip) ? ", " : "",
        (sip) ? WiFi.softAPIP().toString().c_str() : "");
    }

#if defined(USE_ETHERNET)
    eip = EthernetHasIP();
    if (eip) {
      WSContentSend_P(PSTR("%s%s%s (%s)"),          // tasmota-eth.local (192.168.2.13)
        (lip || sip) ? PSTR("<br>") : PSTR("<div class='ts-hostline'>"),
        EthernetHostname(),
        (Mdns.begun) ? PSTR(".local") : "",
        (eip) ? EthernetLocalIP().toString().c_str() : "");
    }
#endif
    if (lip || sip || eip) {
      WSContentSend_P(PSTR("</div>"));
    }
  }
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSendStyle(void) {
  WSContentSendStyle_P(nullptr);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentTextCenterStart(uint32_t color) {
  WSContentSend_P(PSTR("<div style='text-align:center;color:#%06x;'>"), color);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentButton(uint32_t title_index, bool show=true) {
  char action[4];
  char title[100];  // Large to accomodate UTF-16 as used by Russian

  WSContentSend_P(PSTR("<p></p><form id=but%d style=\"display:%s;\" action='%s' method='get'"),
    title_index,
    show ? "block":"none",
    GetTextIndexed(action, sizeof(action), title_index, kButtonAction));
  if (title_index <= BUTTON_RESET_CONFIGURATION) {
    char confirm[100];
    WSContentSend_P(PSTR(" onsubmit='return confirm(\"%s\");'><button name='%s' class='button bred'>%s</button></form>"),
      GetTextIndexed(confirm, sizeof(confirm), title_index, kButtonConfirm),
      (!title_index) ? PSTR("rst") : PSTR("non"),
      GetTextIndexed(title, sizeof(title), title_index, kButtonTitle));
  } else {
    WSContentSend_P(PSTR("><button>%s</button></form>"),
      GetTextIndexed(title, sizeof(title), title_index, kButtonTitle));
  }
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSpaceButton(uint32_t title_index, bool show=true) {
  WSContentSend_P(PSTR("<div></div>"));             // 5px padding
  WSContentButton(title_index, show);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSeparator(uint32_t state) {
  // Send two column separator
  static bool request = false;
  switch (state) {
    case 0:    // Print separator (fall through to WSContentSeparator(1))
      request = true;
    case 1:    // Print separator if needed
      if (request) {
        WSContentSend_P(HTTP_SNS_HR);  // <tr><td colspan=2><hr>{e}
        request = false;
      }
      break;
    case 2:    // Print separator on next WSContentSeparator(1)
      request = true;
      break;
    case 3:    // Don't print separator on next WSContentSeparator(1)
      request = false;
      break;
  }
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_Temp(const char *types, float f_temperature) {
  WSContentSend_PD(HTTP_SNS_F_TEMP, 
    types, 
    Settings->flag2.temperature_resolution,
    &f_temperature,
    TempUnit());
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_Voltage(const char *types, float f_voltage) {
  WSContentSend_PD(HTTP_SNS_F_VOLTAGE, 
    types,
    Settings->flag2.voltage_resolution,
    &f_voltage);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_Current(const char *types, float f_current) {
  WSContentSend_PD(HTTP_SNS_F_CURRENT,
    types,
    Settings->flag2.current_resolution,
    &f_current);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSend_THD(const char *types, float f_temperature, float f_humidity) {
  WSContentSend_Temp(types, f_temperature);

  char parameter[FLOATSZ];
  dtostrfd(f_humidity, Settings->flag2.humidity_resolution, parameter);
  WSContentSend_PD(HTTP_SNS_HUM, types, parameter);
  dtostrfd(CalcTempHumToDew(f_temperature, f_humidity), Settings->flag2.temperature_resolution, parameter);
  WSContentSend_PD(HTTP_SNS_DEW, types, parameter, TempUnit());
#ifdef USE_HEAT_INDEX
  dtostrfd(CalcTemHumToHeatIndex(f_temperature, f_humidity), Settings->flag2.temperature_resolution, parameter);
  WSContentSend_PD(HTTP_SNS_HEATINDEX, types, parameter, TempUnit());
#endif  // USE_HEAT_INDEX
}

/*-------------------------------------------------------------------------------------------*/

void WSContentEnd(void) {
  WSContentFlush();                                // Flush chunk buffer
//  _WSContentSend("");                              // Signal end of chunked content using multiple writes

  // Fix UDP response #23613
  const char *footer_empty = "0\r\n\r\n";
  Webserver->client().write(footer_empty, 5);      // Signal end of chunked content in one write (doesn't clear core _chunked)
  delay(5);

  Webserver->client().stop();
#if defined(USE_MI_ESP32) && !defined(USE_BLE_ESP32)
  MI32resumeScanTask();
#endif // defined(USE_MI_ESP32) && !defined(USE_BLE_ESP32)
}

/*-------------------------------------------------------------------------------------------*/

void WSContentStop(void) {
  if ( WifiIsInManagerMode() && (!Web.initial_config) ) {
    if (WifiConfigCounter()) {
      WSContentSend_P(HTTP_COUNTER);
    }
  }
  WSContentSend_P(HTTP_END, TasmotaGlobal.version, TasmotaGlobal.image_name);
  WSContentEnd();
}

#ifdef USE_WEB_STATUS_LINE
/*-------------------------------------------------------------------------------------------*/
// Display a Sticker in the Status bar (upper right) with a name (MQTT, TlS, VPN)
// and an optional tooltip text to indicate the duration of the connection (or -1 if none)
//
// Convert seconds to a string representing days, hours or minutes.
// The string will contain the most coarse time only, rounded down (61m == 01h, 01h37m == 01h).
void WSContentStatusSticker(const char *msg, const char *attr = NULL);
void WSContentStatusSticker(const char *msg, const char *attr)
{
  if (msg == NULL) { return; }
  if (attr == NULL) { attr = ""; }
  WSContentSend_P(HTTP_STATUS_STICKER, 0xAAAAAA, attr, msg);
}
#endif // USE_WEB_STATUS_LINE

/*********************************************************************************************/

void WebRestart(uint32_t type) {
  // type 0 = restart
  // type 1 = restart after config change
  // type 2 = Checking WiFi Connection - no restart, only refresh page.
  // type 3 = restart after WiFi Connection Test Successful
  // type 4 = type 0 without auto switch to production
  bool prep_switch_partition = false;
  if (0 == type) { prep_switch_partition = true; }
  if (4 == type) { type = 0; }

  bool reset_only = (HTTP_MANAGER_RESET_ONLY == Web.state);

  WSContentStart_P((type) ? PSTR(D_SAVE_CONFIGURATION) : PSTR(D_RESTART), !reset_only);
  WSContentSendStyle();

  WSScriptStart();
  #if ((RESTART_AFTER_INITIAL_WIFI_CONFIG) && (AFTER_INITIAL_WIFI_CONFIG_GO_TO_NEW_IP))
    if (3 == type) {
      WSContentSend_P(PSTR("setTimeout(function(){location.href='http://%s';},%d);"),
        IPForUrl(WiFi.localIP()).c_str(),
        HTTP_RESTART_RECONNECT_TIME
      );
    } else {
      WSContentSend_P(HTTP_SCRIPT_RELOAD_TIME, HTTP_RESTART_RECONNECT_TIME);
    }
  #else
    if (!(3 == type)) {
      WSContentSend_P(HTTP_SCRIPT_RELOAD_TIME, HTTP_RESTART_RECONNECT_TIME);
    }
  #endif
  WSScriptStop();
  if (type) {
    if (!(3 == type)) {
      WSContentSend_P(PSTR("<div style='text-align:center;'><b>%s</b><br><br></div>"),
        (type==2) ? PSTR(D_TRYING_TO_CONNECT) : PSTR(D_CONFIGURATION_SAVED) );
    } else {
#if (AFTER_INITIAL_WIFI_CONFIG_GO_TO_NEW_IP)
      WSContentTextCenterStart(WebColor(COL_TEXT_SUCCESS));
      WSContentSend_P(PSTR(D_SUCCESSFUL_WIFI_CONNECTION "<br><br></div><div style='text-align:center;'>" D_REDIRECTING_TO_NEW_IP "<br><br><a href='http://%_I'>%_I</a><br></div>"),
        (uint32_t)WiFi.localIP(),
        (uint32_t)WiFi.localIP());
#else
      WSContentTextCenterStart(WebColor(COL_TEXT_SUCCESS));
      WSContentSend_P(PSTR(D_SUCCESSFUL_WIFI_CONNECTION "<br><br></div><div style='text-align:center;'>" D_NOW_YOU_CAN_CLOSE_THIS_WINDOW "<br><br></div>"));
#endif
    }
  }
  if (type < 2) {
    WSContentSend_P(HTTP_MSG_RSTRT);
    if (HTTP_MANAGER == Web.state || reset_only) {
      Web.state = HTTP_ADMIN;
    } else {
      WSContentSpaceButton(BUTTON_MAIN);
    }
  }
  if (!(2 == type)) {
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_RESTART));
    ShowWebSource(SRC_WEBGUI);

    if (prep_switch_partition) { EspPrepSwitchPartition(1); }  // Switch to production partition if on safeboot

    TasmotaGlobal.restart_flag = 2;
  }
  WSContentStop();
}

/*********************************************************************************************/



/*********************************************************************************************\
 * HandleRoot
\*********************************************************************************************/

void HandleWifiLogin(void) {
  WSContentStart_P(PSTR(D_CONFIGURE_WIFI), false);
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_CONFIGURE_WIFI), PSTR("Authentication is required before entering WiFi manager settings."));

  WSContentCardStart(PSTR("Sign in"), nullptr);
  WSContentSend_P(HTTP_FORM_LOGIN);
  WSContentCardEnd();

  WSContentCardStart(PSTR("Actions"), nullptr);
  if (HTTP_MANAGER_RESET_ONLY == Web.state) {
    WSContentButton(BUTTON_RESTART);

    WSContentSpaceButton(BUTTON_RESET_CONFIGURATION);

  }
  WSContentCardEnd();

  WSContentStop();
}

/*-------------------------------------------------------------------------------------------*/

void WebGetDeviceCounts(void) {
  Web.buttons_non_light_non_shutter = TasmotaGlobal.devices_present;
  Web.light_shutter_button_mask = 0;       // Bitmask for each light and/or shutter button

//  AddLog(LOG_LEVEL_DEBUG, PSTR("HTP: DP %d, BNLNS %d, SB %08X"), TasmotaGlobal.devices_present, Web.buttons_non_light_non_shutter, Web.light_shutter_button_mask);
}


/*-------------------------------------------------------------------------------------------*/

void HandleRoot(void) {
#ifndef NO_CAPTIVE_PORTAL
  if (CaptivePortal()) { return; }  // If captive portal redirect instead of displaying the page.
#endif  // NO_CAPTIVE_PORTAL

  if (Webserver->hasArg(F("rst"))) {
    WebRestart(0);
    return;
  }

  if (WifiIsInManagerMode()) {
    if (strlen(SettingsText(SET_WEBPWD)) && 
        !(Webserver->hasArg(F("USER1"))) && 
        !(Webserver->hasArg(F("PASS1"))) && 
        HTTP_MANAGER_RESET_ONLY != Web.state) {
      HandleWifiLogin();
    } else {
      if (!strlen(SettingsText(SET_WEBPWD)) || 
          (((Webserver->arg(F("USER1")) == WEB_USERNAME ) &&
            (Webserver->arg(F("PASS1")) == SettingsText(SET_WEBPWD) )) ||
            HTTP_MANAGER_RESET_ONLY == Web.state)) {
        HandleWifiConfiguration();
      } else {
        // wrong user and pass
        HandleWifiLogin();
      }
    }
    return;
  }

  if (HandleRootStatusRefresh()) {
    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_MAIN_MENU));

  /*
  Display GUI with items in following order:
  - Header with module name and device name
  - Dynamic ajax update region
  - Optional power toggle buttons for relays, display and iFan with feedback
  - Optional shutter buttons and slider with feedback
  - Optional light button and slider(s) with feedback
  - Call FUNC_WEB_ADD_MAIN_BUTTON
    - Optional buttons and sliders with feedback
  - Show default main buttons (Configuration, Information, Firmware Upgrade, Tools and Restart)
  */

  char stemp[33];

  WSContentStart_P(PSTR(D_MAIN_MENU));
  WSContentSendStyle();
  WSSendPageScripts(WS_SCRIPT_ROOT | WS_SCRIPT_ROOT_AUTOLOAD);

// REMOVED DEVICE BUTTONS BY TASMOTA ON DASHBOARD
/*


  if (TasmotaGlobal.devices_present) {
    WebGetDeviceCounts();


    if (Web.buttons_non_light_non_shutter) {   // Any non light AND non shutter button - Show toggle buttons
      WSContentSend_P(HTTP_TABLE100);      // "<table style='width:100%%'>"
      WSContentSend_P(PSTR("<tr>"));


        const uint32_t max_columns = 8;
        uint32_t rows = Web.buttons_non_light_non_shutter / max_columns;
        if (Web.buttons_non_light_non_shutter % max_columns) { rows++; }
        uint32_t cols = Web.buttons_non_light_non_shutter / rows;
        if (Web.buttons_non_light_non_shutter % rows) { cols++; }

        uint32_t button_ptr = 0;
        for (uint32_t button_idx = 1; button_idx <= TasmotaGlobal.devices_present; button_idx++) {
          if (bitRead(Web.light_shutter_button_mask, button_idx -1)) { continue; }  // Skip non-sequential light and/or shutter button
          bool set_button = ((button_idx <= MAX_BUTTON_TEXT) && strlen(GetWebButton(button_idx -1)));
          snprintf_P(stemp, sizeof(stemp), PSTR(" %d"), button_idx);
          WSContentSend_P(HTTP_DEVICE_CONTROL, 100 / cols, button_idx, button_idx,
            (set_button) ? HtmlEscape(GetWebButton(button_idx -1)).c_str() : (cols < 5) ? PSTR(D_BUTTON_TOGGLE) : "",
            (set_button) ? "" : (TasmotaGlobal.devices_present > 1) ? stemp : "");
          button_ptr++;
          if (0 == button_ptr % cols) { WSContentSend_P(PSTR("</tr><tr>")); }
        }

      WSContentSend_P(PSTR("</tr></table>"));
    }

  }

  // Init buttons 
  uint32_t max_devices = TasmotaGlobal.devices_present;


  bool use_script = false;
  for (uint32_t idx = 1; idx <= max_devices; idx++) {
    bool not_active = !bitRead(TasmotaGlobal.power, idx -1);


    if (not_active) {
      if (!use_script) {
        use_script = true;
        WSContentSend_P(PSTR("<script>"));
      }
      WSContentSend_P(PSTR("eb('o%d').style.background='var(--c_btnoff)';"), idx);
    }
  }
  if (use_script) {
    WSContentSend_P(PSTR("</script>"));
  }


*/

  WSContentRefreshSectionBegin(WS_SECTION_ROOT_LIVE); // Prepare for root widgets
    WSContentSendRootLive();
  WSContentSectionEnd();


  XdrvXsnsCall(FUNC_WEB_ADD_MAIN_BUTTON);
  WSContentStop();
}

/*-------------------------------------------------------------------------------------------*\
 * HandleRootStatusRefresh
\*-------------------------------------------------------------------------------------------*/

void WSContentSendRootLive(void) {
  // Rezervované miesto pre budúce auto-refresh widgety na root page.
  // Zatiaľ zámerne prázdne.
}

void WSContentSendSensorLive(void) {
  char svalue[32];

  WSContentSend_P(PSTR("{t}"));
  WSContentSeparator(3);

  if (Settings->web_time_end) {
    WSContentSend_P(PSTR("{s}" D_TIME_OF_DAY "{m}%s{e}"),
      GetDateAndTime(DT_LOCAL).substring(Settings->web_time_start, Settings->web_time_end).c_str());
    WSContentSeparator(0);
  }

  XsnsXdrvCall(FUNC_WEB_SENSOR);
  WSContentSend_P(PSTR("</table>"));

  if (!Settings->flag6.gui_no_state_text) {
    if (!Web.buttons_non_light_non_shutter) {
      WebGetDeviceCounts();
    }

    if ((Web.buttons_non_light_non_shutter > 0) &&
        (Web.buttons_non_light_non_shutter <= 8)) {
      WSContentSend_P(PSTR("{t}<tr>"));

      uint32_t cols = Web.buttons_non_light_non_shutter;
      uint32_t fontsize = (cols < 5) ? 70 - (cols * 8) : 32;
      uint32_t button_ptr = 0;

      for (uint32_t button_idx = 1; button_idx <= TasmotaGlobal.devices_present; button_idx++) {
        if (bitRead(Web.light_shutter_button_mask, button_idx -1)) { continue; }

        bool power_state = bitRead(TasmotaGlobal.power, button_idx -1);
        snprintf_P(svalue, sizeof(svalue), PSTR("%d"), power_state);

        WSContentSend_P(HTTP_DEVICE_STATE,
          100 / cols,
          (power_state) ? PSTR("bold") : PSTR("normal"),
          fontsize,
          (cols < 5) ? GetStateText(power_state) : svalue);

        button_ptr++;
        if (button_ptr >= Web.buttons_non_light_non_shutter) { break; }
      }

      WSContentSend_P(PSTR("</tr></table>"));
    }
  }
}

const WSRefreshSection kWSRefreshSections[] = {
  { WS_SECTION_TOPBAR_STATUS, WSContentSendToolbarStatusInner },
  { WS_SECTION_ROOT_LIVE,     WSContentSendRootLive },
  { WS_SECTION_SENSOR_LIVE,   WSContentSendSensorLive }
};

void WSSendRequestedRefreshSections(void) {
  String rf = Webserver->arg(F("rf"));
  const char *rf_list = rf.length() ? rf.c_str() : nullptr;

  // bezpečný fallback:
  // ak klient neposlal rf zoznam, obnov len topbar status
  if (!rf_list) {
    WSSendRefreshFragmentBegin(WS_SECTION_TOPBAR_STATUS);
    WSContentSendToolbarStatusInner();
    WSSendRefreshFragmentEnd();
    return;
  }

  for (uint32_t i = 0; i < nitems(kWSRefreshSections); i++) {
    if (WSRefreshListContains(rf_list, kWSRefreshSections[i].id)) {
      WSSendRefreshFragmentBegin(kWSRefreshSections[i].id);
      kWSRefreshSections[i].render();
      WSSendRefreshFragmentEnd();
    }
  }
}

bool WebUpdateSliderTime(void) {
  uint32_t slider_update_time = millis();
  if (0 == Web.slider_update_time) {
    Web.slider_update_time = slider_update_time + Settings->web_refresh;  // Allow other users to sync screen
  }
  else if (slider_update_time > Web.slider_update_time) {
    Web.slider_update_time = 1;  // Allow multiple updates
    return true;
  }
  return false;
}

/*-------------------------------------------------------------------------------------------*/

bool HandleRootStatusRefresh(void) {
  if (!WebAuthenticate()) {
    Webserver->requestAuthentication();
    return true;
  }

  if (!Webserver->hasArg("m")) {     // Status refresh requested
    return false;
  }


  char tmp[8];                       // WebGetArg numbers only
  char svalue[32];                   // Command and number parameter
  char webindex[5];                  // WebGetArg name

  WebGetArg(PSTR("o"), tmp, sizeof(tmp));  // 1 - 32 Device number for button Toggle or Fanspeed
  if (strlen(tmp)) {
    ShowWebSource(SRC_WEBGUI);
    uint32_t device = atoi(tmp);
      ExecuteCommandPower(device, POWER_TOGGLE, SRC_IGNORE);
  }


#ifdef USE_ZIGBEE
  WebGetArg(PSTR("zbj"), tmp, sizeof(tmp));
  if (strlen(tmp)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("ZbPermitJoin"));
    ExecuteWebCommand(svalue);
  }
  WebGetArg(PSTR("zbr"), tmp, sizeof(tmp));
  if (strlen(tmp)) {
    snprintf_P(svalue, sizeof(svalue), PSTR("ZbMap"));
    ExecuteWebCommand(svalue);
  }
#endif // USE_ZIGBEE

  XsnsXdrvCall(FUNC_WEB_GET_ARG);


  WSContentBegin(200, CT_HTML);

  bool has_js_fragment = false;
/*


  if (TasmotaGlobal.devices_present) {
    // Update changed web buttons
    uint32_t max_devices = TasmotaGlobal.devices_present;


    WSContentSend_P(HTTP_MSG_EXEC_JAVASCRIPT);  // "<img style='display:none;' src onerror=\""
    msg_exec_javascript = true;
    for (uint32_t idx = 1; idx <= max_devices; idx++) {
      bool active = bitRead(TasmotaGlobal.power, idx -1);

      WSContentSend_P(PSTR("eb('o%d').style.background='var(--c_btn%s)';"),
        idx, (active) ? PSTR("") : PSTR("off"));
    }
  }
*/

  if (has_js_fragment) {
    WSSendRefreshFragmentEnd();
  }

  WSSendRequestedRefreshSections();

  if (1 == Web.slider_update_time) {
    Web.slider_update_time = 0;
  }

  WSContentSend_P(PSTR("\n\n"));
  WSContentEnd();

  return true;
}


/*********************************************************************************************\
 * HandleConfiguration
\*********************************************************************************************/

void HandleConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURATION));

  WSContentStart_P(PSTR(D_CONFIGURATION));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_CONFIGURATION), PSTR("Select a configuration section below."));

  WSContentCardStart(PSTR(D_CONFIGURATION), nullptr);
  WSContentActionsStart();
  WSContentActionButton(PSTR("md"), PSTR(D_CONFIGURE_MODULE));
  WSContentActionButton(PSTR("wi"), PSTR(D_CONFIGURE_WIFI));
  if (Settings->flag.mqtt_enabled) {
    WSContentActionButton(PSTR("mq"), PSTR(D_CONFIGURE_MQTT));
  }
  WSContentActionButton(PSTR("lg"), PSTR(D_CONFIGURE_LOGGING));
  WSContentActionButton(PSTR("co"), PSTR(D_CONFIGURE_OTHER));
  WSContentActionsEnd();
  WSContentCardEnd();

  /* TODO
  WSContentButton(BUTTON_MODULE);
  WSContentButton(BUTTON_WIFI);

  XdrvXsnsCall(FUNC_WEB_ADD_BUTTON);

  WSContentButton(BUTTON_LOGGING);
  WSContentButton(BUTTON_OTHER);
*/

  WSContentStop();
}



/*********************************************************************************************\
 * HandleModuleConfiguration
\*********************************************************************************************/
static String WebGetGpioLabel(uint32_t sensor_type) {
  char stemp1[TOPSZ];
  char sindex[4] = { 0 };

  uint32_t sensor_name_idx = BGPIO(sensor_type);
  uint32_t nice_list_search = sensor_type & 0xFFE0;

  for (uint32_t j = 0; j < nitems(kGpioNiceList); j++) {
    uint32_t nls_idx = pgm_read_word(&kGpioNiceList[j]);
    if (((nls_idx & 0xFFE0) == nice_list_search) && ((nls_idx & 0x001F) > 0)) {
      snprintf_P(sindex, sizeof(sindex), PSTR("%d"), (sensor_type & 0x001F) + 1);
      break;
    }
  }

  const char *sensor_names = kSensorNames;
  if (sensor_name_idx > GPIO_FIX_START) {
    sensor_name_idx = sensor_name_idx - GPIO_FIX_START - 1;
    sensor_names = kSensorNamesFixed;
  }

  String label = GetTextIndexed(stemp1, sizeof(stemp1), sensor_name_idx, sensor_names);
  label += sindex;
  return label;
}


void HandleModuleConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_MODULE));

  myio template_gp;
  TemplateGpios(&template_gp);

  WSContentStart_P(PSTR(D_CONFIGURE_MODULE));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_CONFIGURE_MODULE), PSTR("Read-only GPIO assignments from the build-time module definition."));

  WSContentCardStart(PSTR(D_MODULE_PARAMETERS), nullptr);
  WSContentSend_P(PSTR("<p><b>" D_MODULE_TYPE "</b>: %s</p>"), AnyModuleName(MODULE).c_str());

  WSContentSend_P(PSTR(
    "<table class='ts-table'>"
      "<tr><th>GPIO</th><th>Function</th><th>Value</th></tr>"
  ));

  for (uint32_t i = 0; i < nitems(template_gp.io); i++) {
    uint32_t sensor_type = template_gp.io[i];
    if ((sensor_type > GPIO_NONE) && (sensor_type < AGPIO(GPIO_USER))) {
      WSContentSend_P(PSTR("<tr><td><b>GPIO%d</b></td><td>%s</td><td>%d</td></tr>"),
        i,
        WebGetGpioLabel(sensor_type).c_str(),
        sensor_type);
    }
  }

  WSContentSend_P(PSTR("</table>"));
  WSContentCardEnd();
  WSContentStop();
}


/*********************************************************************************************\
 * HandleWifiConfiguration
\*********************************************************************************************/

void HandleWifiConfiguration(void) {
  char tmp[TOPSZ];

  if (!HttpCheckPriviledgedAccess(!WifiIsInManagerMode())) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_WIFI));

  if (Webserver->hasArg(F("save")) && HTTP_MANAGER_RESET_ONLY != Web.state) {
    if (WifiIsInManagerMode()) {
      if (WIFI_NOT_TESTING == Wifi.wifiTest) {
        if (MAX_WIFI_OPTION == Wifi.old_wificonfig) { Wifi.old_wificonfig = Settings->sta_config; }
        TasmotaGlobal.wifi_state_flag = Settings->sta_config = WIFI_MANAGER;
        Wifi.save_data_counter = TasmotaGlobal.save_data_counter;
      }

      Wifi.wifi_test_counter = 9;
      Wifi.wifiTest = WIFI_TESTING;
      TasmotaGlobal.save_data_counter = 0;
      Settings->save_data = 0;
      TasmotaGlobal.sleep = 0;
      TasmotaGlobal.restart_flag = 0;
      TasmotaGlobal.ota_state_flag = 0;

      WebGetArg(PSTR("s1"), tmp, sizeof(tmp));
      SettingsUpdateText(SET_STASSID1, tmp);
      WebGetArg(PSTR("p1"), tmp, sizeof(tmp));
      SettingsUpdateText(SET_STAPWD1, tmp);

      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CONNECTING_TO_AP " %s " D_AS " %s ..."),
        SettingsText(SET_STASSID1), TasmotaGlobal.hostname);

      WiFiHelper::begin(SettingsText(SET_STASSID1), SettingsText(SET_STAPWD1));

      WebRestart(2);
    } else {
      WifiSaveSettings();
      WebRestart(1);
    }
    return;
  }

  if (WIFI_TEST_FINISHED == Wifi.wifiTest) {
    Wifi.wifiTest = WIFI_NOT_TESTING;
    if (Wifi.wifi_test_AP_TIMEOUT) {
      WebRestart(1);
    } else {
#if (RESTART_AFTER_INITIAL_WIFI_CONFIG)
      WebRestart(3);
#else
      HandleRoot();
#endif
    }
  }

  WSContentStart_P(PSTR(D_CONFIGURE_WIFI), !WifiIsInManagerMode());

  #ifdef USE_ENHANCED_GUI_WIFI_SCAN
    WSContentSendStyle_P("%s", HTTP_HEAD_STYLE_SSI);
  #else
    WSContentSendStyle();
  #endif

  uint16_t wifi_scripts = WS_SCRIPT_WIFI;
  if (WifiIsInManagerMode()) {
    wifi_scripts |= WS_SCRIPT_HIDE;
  }
  if (WIFI_TESTING == Wifi.wifiTest) {
    wifi_scripts |= WS_SCRIPT_RELOAD;
  }

  WSSendPageScripts(wifi_scripts, HTTP_RESTART_RECONNECT_TIME);

  WSContentPageHeader(
    PSTR(D_CONFIGURE_WIFI),
    WifiIsInManagerMode() ?
      PSTR("Use WiFi manager to select a network and validate connectivity before leaving access-point mode.") :
      PSTR("Manage station credentials, hostname and optional network settings.")
  );

  bool limitScannedNetworks = true;

  if (HTTP_MANAGER_RESET_ONLY != Web.state) {
    WSContentCardStart(PSTR("Available networks"), nullptr);

    if (WIFI_TESTING == Wifi.wifiTest) {
      WSContentSend_P(PSTR("<div class='ts-note ts-soft'>" D_TRYING_TO_CONNECT "...</div>"));
      limitScannedNetworks = false;
    } else {
      if (Webserver->hasArg(F("scan"))) { limitScannedNetworks = false; }

      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI "Scanning..."));
      int n = WiFi.scanNetworks(true);
      while (n < 0) {
        delay(50);
        n = WiFi.scanComplete();
      }
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_SCAN_DONE));

      if (0 == n) {
        AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_NO_NETWORKS_FOUND));
        WSContentSend_P(PSTR("<div class='ts-note'>" D_NO_NETWORKS_FOUND "</div>"));
        limitScannedNetworks = false;
      } else {
        int indices[n];
        for (uint32_t i = 0; i < n; i++) {
          indices[i] = i;
        }

        for (uint32_t i = 0; i < n; i++) {
          for (uint32_t j = i + 1; j < n; j++) {
            if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
              std::swap(indices[i], indices[j]);
            }
          }
        }

        uint32_t networksToShow = n;
        if ((limitScannedNetworks) && (networksToShow > MAX_WIFI_NETWORKS_TO_SHOW)) {
          networksToShow = MAX_WIFI_NETWORKS_TO_SHOW;
        }

        if (WifiIsInManagerMode()) {
          WSContentSend_P(PSTR("<div class='ts-note'>" D_SELECT_YOUR_WIFI_NETWORK "</div><br>"));
        }

        WSContentSend_P(PSTR("<div class='ts-scanlist'>"));

#ifdef USE_ENHANCED_GUI_WIFI_SCAN
        bool skipduplicated;
        int ssid_showed = 0;
        for (uint32_t i = 0; i < networksToShow; i++) {
          if (indices[i] < n) {
            int32_t rssi = WiFi.RSSI(indices[i]);
            String ssid = WiFi.SSID(indices[i]);

            String ssid_copy = ssid;
            if (!ssid_copy.length()) { ssid_copy = F("no_name"); }

            if (!limitScannedNetworks) {
              WSContentSend_P(PSTR("<div><a href='#p' onclick='c(this)'>%s</a><br>"),
                HtmlEscape(ssid_copy).c_str());
            }

            skipduplicated = false;
            String nextSSID = "";
#ifdef USE_HIGHLIGHT_CONNECTED_AP
            bool HighlightAP;
#endif
            for (uint32_t j = 0; j < n; j++) {
              if ((indices[j] < n) && ((nextSSID = WiFi.SSID(indices[j])) == ssid)) {
                if (!skipduplicated) {
                  rssi = WiFi.RSSI(indices[j]);
                  uint8_t rssi_as_quality = WifiGetRssiAsQuality(rssi);
                  uint8_t num_bars = changeUIntScale(rssi_as_quality, 0, 100, 0, 4);

                  WSContentSend_P(PSTR("<div title='%d%% (%d dBm)'>"), rssi_as_quality, rssi);
                  if (limitScannedNetworks) {
                    WSContentSend_P(PSTR("<a href='#p' onclick='c(this)'>%s</a><span class='q'><div class='si'>"), HtmlEscape(ssid_copy).c_str());
                    ssid_showed++;
                    skipduplicated = true;
#ifdef USE_HIGHLIGHT_CONNECTED_AP
                    HighlightAP = ssid_copy == WiFi.SSID();
#endif
                  } else {
                    WSContentSend_P(PSTR("%s<span class='q'>(%d) <div class='si'>"),
                      WiFi.BSSIDstr(indices[j]).c_str(),
                      WiFi.channel(indices[j]));
#ifdef USE_HIGHLIGHT_CONNECTED_AP
                    HighlightAP = WiFi.BSSIDstr(indices[j]) == WiFi.BSSIDstr();
#endif
                  }

                  for (uint8_t k = 0; k < 4; k++) {
#ifdef USE_HIGHLIGHT_CONNECTED_AP
                    WSContentSend_P(PSTR("<i class='b%d%s'%s></i>"),
                      k,
                      (k >= num_bars) ? PSTR(" o30") : PSTR(""),
                      HighlightAP ? PSTR(" style='background-color:var(--c_btn);'") : PSTR(""));
#else
                    WSContentSend_P(PSTR("<i class='b%d%s'></i>"),
                      k,
                      (k >= num_bars) ? PSTR(" o30") : PSTR(""));
#endif
                  }
                  WSContentSend_P(PSTR("</div></span></div>"));
                } else {
                  if (ssid_showed <= networksToShow) { networksToShow++; }
                }
                indices[j] = n;
              }
              delay(0);
            }
            if (!limitScannedNetworks) {
              WSContentSend_P(PSTR("</div>"));
            }
          }
        }
#else
        for (uint32_t i = 0; i < n; i++) {
          if (-1 == indices[i]) { continue; }
          String cssid = WiFi.SSID(indices[i]);
          uint32_t cschn = WiFi.channel(indices[i]);
          for (uint32_t j = i + 1; j < n; j++) {
            if ((cssid == WiFi.SSID(indices[j])) && (cschn == WiFi.channel(indices[j]))) {
              indices[j] = -1;
            }
          }
        }

        for (uint32_t i = 0; i < networksToShow; i++) {
          if (-1 == indices[i]) { continue; }
          int32_t rssi = WiFi.RSSI(indices[i]);
          int quality = WifiGetRssiAsQuality(rssi);
          String ssid_copy = WiFi.SSID(indices[i]);
          if (!ssid_copy.length()) { ssid_copy = F("no_name"); }
          WSContentSend_P(PSTR("<div><a href='#p' onclick='c(this)'>%s</a>&nbsp;(%d)&nbsp;<span class='q'>%d%% (%d dBm)</span></div>"),
            HtmlEscape(ssid_copy).c_str(),
            WiFi.channel(indices[i]),
            quality, rssi);
          delay(0);
        }
#endif
        WSContentSend_P(PSTR("</div>"));
      }
    }

    WSContentSend_P(PSTR("<br><div><a href='/wi?scan='>%s</a></div>"),
      (limitScannedNetworks) ? PSTR(D_SHOW_MORE_WIFI_NETWORKS) : PSTR(D_SCAN_FOR_WIFI_NETWORKS));
    WSContentCardEnd();

    WSContentSend_P(HTTP_FIELDSET_LEGEND, PSTR(D_WIFI_PARAMETERS));
    WSContentSend_P(HTTP_FORM_GET_ACTION, PSTR("wi"));
    WSContentSend_P(HTTP_FORM_WIFI_PART1,
      (WifiIsInManagerMode()) ? "" : PSTR(" (" STA_SSID1 ")"),
      SettingsTextEscaped(SET_STASSID1).c_str());

    if (WifiIsInManagerMode()) {
      WSContentSend_P(PSTR("></div>"));
    } else {
#ifdef USE_CORS
      WSContentSend_P(HTTP_FORM_WIFI_PART2,
        SettingsTextEscaped(SET_STASSID2).c_str(),
        WIFI_HOSTNAME, WIFI_HOSTNAME,
        SettingsTextEscaped(SET_HOSTNAME).c_str(),
        SettingsTextEscaped(SET_CORS).c_str());
#else
      WSContentSend_P(HTTP_FORM_WIFI_PART2,
        SettingsTextEscaped(SET_STASSID2).c_str(),
        WIFI_HOSTNAME, WIFI_HOSTNAME,
        SettingsTextEscaped(SET_HOSTNAME).c_str());
#endif
    }

    WSContentSend_P(HTTP_FORM_END);
  }



  if (WifiIsInManagerMode()) {
    WSContentCardStart(PSTR("Actions"), nullptr);
    if (WIFI_TESTING == Wifi.wifiTest) {
      WSContentSend_P(PSTR("<div class='ts-note ts-soft'>" D_TRYING_TO_CONNECT " %s</div>"),
        SettingsTextEscaped(SET_STASSID1).c_str());
    } else if (WIFI_TEST_FINISHED_BAD == Wifi.wifiTest) {
      WSContentSend_P(PSTR("<div class='ts-note ts-danger'>" D_CONNECT_FAILED_TO " %s<br>" D_CHECK_CREDENTIALS "</div>"),
        SettingsTextEscaped(SET_STASSID1).c_str());
    }

    WSContentSend_P(PSTR("<div id=butmod style=\"display:%s;\"></div><p></p><form id=butmo style=\"display:%s;\"><button type='button' onclick='hidBtns()'>" D_SHOW_MORE_OPTIONS "</button></form>"),
      (WIFI_TEST_FINISHED_BAD == Wifi.wifiTest) ? "none" : Web.initial_config ? "block" : "none",
      Web.initial_config ? "block" : "none"
    );

    WSContentSend_P(PSTR("<div id='wm-restore' style='display:%s;'>"), Web.initial_config ? "none" : "block");
    WSContentSpaceButton(BUTTON_RESTORE, true);
    WSContentSend_P(PSTR("</div>"));

    WSContentSend_P(PSTR("<div id='wm-reset' style='display:%s;'>"), Web.initial_config ? "none" : "block");
    WSContentButton(BUTTON_RESET_CONFIGURATION, true);
    WSContentSend_P(PSTR("</div>"));
    WSContentSend_P(PSTR("<div id='wm-restart' style='display:%s;'>"), Web.initial_config ? "none" : "block");
    WSContentSpaceButton(BUTTON_RESTART, true);
    WSContentSend_P(PSTR("</div>"));
    WSContentCardEnd();
  } 

  

  WSContentStop();
}

/*-------------------------------------------------------------------------------------------*/

void WifiSaveSettings(void) {
  String cmnd = F(D_CMND_BACKLOG "0 ");
  cmnd += AddWebCommand(PSTR(D_CMND_HOSTNAME), PSTR("h"), PSTR("1"));
#ifdef USE_CORS
  cmnd += AddWebCommand(PSTR(D_CMND_CORS), PSTR("c"), PSTR("1"));
#endif  // USE_CORS
  cmnd += AddWebCommand(PSTR(D_CMND_SSID "1"), PSTR("s1"), PSTR("1"));
  cmnd += AddWebCommand(PSTR(D_CMND_SSID "2"), PSTR("s2"), PSTR("1"));
  cmnd += AddWebCommand(PSTR(D_CMND_PASSWORD "3"), PSTR("p1"), PSTR("\""));
  cmnd += AddWebCommand(PSTR(D_CMND_PASSWORD "4"), PSTR("p2"), PSTR("\""));
  ExecuteWebCommand((char*)cmnd.c_str());
}

/*********************************************************************************************\
 * HandleLoggingConfiguration
\*********************************************************************************************/

void HandleLoggingConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_LOGGING));

  if (Webserver->hasArg("save")) {
    LoggingSaveSettings();
    HandleConfiguration();
    return;
  }

  WSContentStart_P(PSTR(D_CONFIGURE_LOGGING));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_CONFIGURE_LOGGING), PSTR("Adjust verbosity for serial, web, MQTT and syslog outputs."));

  char stemp1[45];
  char stemp2[32];
  uint8_t dlevel[4] = { LOG_LEVEL_INFO, LOG_LEVEL_INFO, LOG_LEVEL_NONE, LOG_LEVEL_NONE };

  WSContentFormStart(PSTR(D_LOGGING_PARAMETERS), PSTR("lg"));

  for (uint32_t idx = 0; idx < 4; idx++) {
    if ((2 == idx) && !Settings->flag.mqtt_enabled) { continue; }

    uint32_t llevel = (0 == idx) ? Settings->seriallog_level :
                      (1 == idx) ? Settings->weblog_level :
                      (2 == idx) ? Settings->mqttlog_level :
                                   Settings->syslog_level;

    WSContentSend_P(PSTR("<div class='ts-field'>"
                         "<label>%s <span class='ts-field-note'>(%s)</span></label>"
                         "<select id='l%d'>"),
      GetTextIndexed(stemp1, sizeof(stemp1), idx, kLoggingOptions),
      GetTextIndexed(stemp2, sizeof(stemp2), dlevel[idx], kLoggingLevels),
      idx);

    for (uint32_t i = LOG_LEVEL_NONE; i <= LOG_LEVEL_DEBUG_MORE; i++) {
      WSContentSend_P(PSTR("<option%s value='%d'>%d %s</option>"),
        (i == llevel) ? PSTR(" selected") : PSTR(""),
        i, i,
        GetTextIndexed(stemp1, sizeof(stemp1), i, kLoggingLevels));
    }

    WSContentSend_P(PSTR("</select></div>"));
  }

  WSContentSend_P(HTTP_FORM_LOG,
    SettingsTextEscaped(SET_SYSLOG_HOST).c_str(),
    Settings->syslog_port,
    Settings->tele_period);

  WSContentFormEnd();
  WSContentStop();
}
/*-------------------------------------------------------------------------------------------*/

void LoggingSaveSettings(void) {
  String cmnd = F(D_CMND_BACKLOG "0 ");
  cmnd += AddWebCommand(PSTR(D_CMND_SERIALLOG), PSTR("l0"), STR(SERIAL_LOG_LEVEL));
  cmnd += AddWebCommand(PSTR(D_CMND_WEBLOG), PSTR("l1"), STR(WEB_LOG_LEVEL));
  cmnd += AddWebCommand(PSTR(D_CMND_MQTTLOG), PSTR("l2"), STR(MQTT_LOG_LEVEL));
  cmnd += AddWebCommand(PSTR(D_CMND_SYSLOG), PSTR("l3"), STR(SYS_LOG_LEVEL));
  cmnd += AddWebCommand(PSTR(D_CMND_LOGHOST), PSTR("lh"), PSTR("1"));
  cmnd += AddWebCommand(PSTR(D_CMND_LOGPORT), PSTR("lp"), PSTR("1"));
  cmnd += AddWebCommand(PSTR(D_CMND_TELEPERIOD), PSTR("lt"), PSTR("1"));
  ExecuteWebCommand((char*)cmnd.c_str());
}

/*********************************************************************************************\
 * HandleLoggingConfiguration
\*********************************************************************************************/

void HandleOtherConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONFIGURE_OTHER));

  if (Webserver->hasArg(F("save"))) {
    OtherSaveSettings();
    WebRestart(1);
    return;
  }

  WSContentStart_P(PSTR(D_CONFIGURE_OTHER));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_CONFIGURE_OTHER), PSTR("Security, identity and web-facing runtime options."));

  WSContentFormStart(PSTR(D_OTHER_PARAMETERS), PSTR("co"));

  WSContentSend_P(HTTP_FORM_OTHER,
    (Settings->flag5.disable_referer_chk) ? PSTR(" checked") : PSTR(""),
    (Settings->flag.mqtt_enabled) ? PSTR(" checked") : PSTR(""),
    SettingsTextEscaped(SET_FRIENDLYNAME1).c_str(),
    SettingsTextEscaped(SET_DEVICENAME).c_str());

  char stemp[32];
  uint32_t maxfn = (TasmotaGlobal.devices_present > MAX_FRIENDLYNAMES) ? MAX_FRIENDLYNAMES :
                   (!TasmotaGlobal.devices_present) ? 1 :
                   TasmotaGlobal.devices_present;

  for (uint32_t i = 0; i < maxfn; i++) {
    snprintf_P(stemp, sizeof(stemp), PSTR("%d"), i + 1);

    WSContentSend_P(PSTR("<div class='ts-field'>"
                         "<label>" D_FRIENDLY_NAME " %d <span class='ts-field-note'>(" FRIENDLY_NAME "%s)</span></label>"
                         "<input id='a%d' placeholder=\"" FRIENDLY_NAME "%s\" value=\"%s\">"
                         "</div>"),
      i + 1,
      (i) ? stemp : "",
      i,
      (i) ? stemp : "",
      SettingsTextEscaped(SET_FRIENDLYNAME1 + i).c_str());
  }

  WSContentFormEnd();

  WSContentCardStart(PSTR("Actions"), nullptr);
  WSContentActionsStart();
  WSContentActionButton(PSTR("dl"), PSTR(D_BACKUP_CONFIGURATION));
  WSContentActionButton(PSTR("rs"), PSTR(D_RESTORE_CONFIGURATION));
  WSContentActionButton(PSTR("rt"), PSTR(D_RESET_CONFIGURATION));
  WSContentActionsEnd();
  WSContentCardEnd();

  WSContentStop();
}
/*-------------------------------------------------------------------------------------------*/

void OtherSaveSettings(void) {
  String cmnd = F(D_CMND_BACKLOG "0 ");
  cmnd += AddWebCommand(PSTR(D_CMND_WEBPASSWORD "2"), PSTR("wp"), PSTR("\""));
  cmnd += F(";" D_CMND_SO "3 ");
  cmnd += Webserver->hasArg(F("b1"));
  cmnd += F(";" D_CMND_SO "128 ");
  cmnd += Webserver->hasArg(F("b3"));
  cmnd += AddWebCommand(PSTR(D_CMND_DEVICENAME), PSTR("dn"), PSTR("\""));
  char webindex[5];
  char cmnd2[24];                             // ";Module 0;Template "
  for (uint32_t i = 0; i < MAX_FRIENDLYNAMES; i++) {
    snprintf_P(webindex, sizeof(webindex), PSTR("a%d"), i);
    snprintf_P(cmnd2, sizeof(cmnd2), PSTR(D_CMND_FN "%d"), i +1);
    cmnd += AddWebCommand(cmnd2, webindex, PSTR("\""));
  }

  ExecuteWebCommand((char*)cmnd.c_str());
}

/*********************************************************************************************\
 * HandleBackupConfiguration
\*********************************************************************************************/

void HandleBackupConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_BACKUP_CONFIGURATION));

  uint32_t config_len = SettingsConfigBackup();
  if (!config_len) { return; }    // Unable to allocate buffer

  WiFiClient myClient = Webserver->client();
  Webserver->setContentLength(config_len);

  char attachment[TOPSZ];
  snprintf_P(attachment, sizeof(attachment), PSTR("attachment; filename=%s"), SettingsConfigFilename().c_str());
  Webserver->sendHeader(F("Content-Disposition"), attachment);

  WSSend(200, CT_APP_STREAM, "");
  myClient.write((const char*)settings_buffer, config_len);

  SettingsBufferFree();
}

/*********************************************************************************************\
 * HandleResetConfiguration
\*********************************************************************************************/

void HandleResetConfiguration(void) {
  if (!HttpCheckPriviledgedAccess(!WifiIsInManagerMode())) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_RESET_CONFIGURATION));

  WSContentStart_P(PSTR(D_RESET_CONFIGURATION), !WifiIsInManagerMode());
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_RESET_CONFIGURATION), PSTR("The current configuration will be erased and the device will restart."));

  WSContentCardStart(PSTR("Reset pending"), nullptr);
  WSContentSend_P(PSTR("<div class='ts-center ts-danger'>" D_CONFIGURATION_RESET "</div>"));
  WSContentSend_P(HTTP_MSG_RSTRT);
  WSContentCardEnd();

  WSContentStop();

  char command[CMDSZ];
  snprintf_P(command, sizeof(command), PSTR(D_CMND_RESET " 1"));
  ExecuteWebCommand(command);
}

/*********************************************************************************************\
 * HandleRestoreConfiguration
\*********************************************************************************************/

void HandleRestoreConfiguration(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_RESTORE_CONFIGURATION));

  WSContentStart_P(PSTR(D_RESTORE_CONFIGURATION));
  WSContentSendStyle();
  WSSendPageScripts(WS_SCRIPT_UPLOAD);
  WSContentPageHeader(PSTR(D_RESTORE_CONFIGURATION), PSTR("Restore settings from a previously downloaded configuration file."));

  WSContentCardStart(PSTR(D_RESTORE_CONFIGURATION), nullptr);
  WSContentSend_P(HTTP_DIV_F1_BLOCK);
  WSContentSend_P(PSTR("<div class='ts-note'>Choose a backup file and upload it to replace the active configuration.</div>"));
  WSContentSend_P(HTTP_FORM_RESTORE_CARD, PSTR(D_START_RESTORE));
  WSContentCardEnd();

  WSContentStop();

  Web.upload_file_type = UPL_SETTINGS;
}

/*********************************************************************************************\
 * HandleInformation
\*********************************************************************************************/

void WSContentSeparatorI(uint32_t size) {
  WSContentSend_P(PSTR("</td></tr><tr><td colspan=2><hr style='font-size:2px'%s>"),
    (1 == size)?" size=1":"");
//  WSContentSend_P(PSTR("</td></tr><tr><td colspan=2><hr style='font-size:%dpx'/>"), size);
//  WSContentSend_P(PSTR("</td></tr><tr><td colspan=2><hr style='border_top:%dpx solid'/>"), size);
//  WSContentSend_P(PSTR("</td></tr><tr><td colspan=2 style='border-bottom:%dpx solid #ccc;'>"), size);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSeparatorIFat(void) {
//  WSContentSend_P(PSTR("}1}2&nbsp;"));      // Empty line = </td></tr><tr><th></th><td>&nbsp;
  WSContentSeparatorI(2);
}

/*-------------------------------------------------------------------------------------------*/

void WSContentSeparatorIThin(void) {
  WSContentSeparatorI(1);
}

/*-------------------------------------------------------------------------------------------*/

void HandleInformation(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_INFORMATION));

  WSContentStart_P(PSTR(D_INFORMATION));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_INFORMATION), PSTR("Choose which information page you want to open."));

  WSContentCardStart(PSTR(D_INFORMATION), nullptr);
  WSContentSend_P(PSTR(
    "<div class='ts-actions-grid'>"
      "<form method='get' action='if'><button>Device Info</button></form>"
      "<form method='get' action='is'><button>Sensors</button></form>"
    "</div>"
  ));
  WSContentCardEnd();

  WSContentStop();
}


void HandleInformationSensors(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  if (HandleRootStatusRefresh()) {
    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP "Sensors"));

  WSContentStart_P(PSTR("Sensors"));
  WSContentSendStyle();
  WSSendPageScripts(WS_SCRIPT_ROOT | WS_SCRIPT_ROOT_AUTOLOAD);

  WSContentPageHeader(PSTR("Sensors"), PSTR("Live sensor values and device status refreshed automatically."));
  WSContentCardStart(PSTR("Sensors"), nullptr);
  WSContentRefreshSectionBegin(WS_SECTION_SENSOR_LIVE);
    WSContentSendSensorLive();
  WSContentSectionEnd();
  WSContentCardEnd();

  WSContentStop();
}



void HandleInformationDevice(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP "Device Info"));

  float freemem = ((float)ESP_getFreeHeap()) / 1024;
  char stopic[TOPSZ];
  char label[64];
  char value[256];
  char fmem[32];

  WSContentStart_P(PSTR("Device Info"));
  WSContentSendStyle();
  WSContentPageHeader(PSTR("Device Info"), PSTR("Firmware, network, MQTT and hardware details."));

  WSContentCardStart(PSTR("Device Info"), nullptr);
  WSContentSend_P(PSTR("<table class='ts-table'>"));

  // Firmware / runtime
  WSContentSend_P(PSTR("<tr><th>" D_PROGRAM_VERSION "</th><td>%s %s %s</td></tr>"),
    TasmotaGlobal.version,
    TasmotaGlobal.image_name,
    GetCodeCores().c_str());

  WSContentSend_P(PSTR("<tr><th>" D_BUILD_DATE_AND_TIME "</th><td>%s</td></tr>"),
    GetBuildDateAndTime().c_str());

  WSContentSend_P(PSTR("<tr><th>" D_CORE_AND_SDK_VERSION "</th><td>" ARDUINO_CORE_RELEASE "/%s</td></tr>"),
    ESP.getSdkVersion());

  WSContentSend_P(PSTR("<tr><th>" D_UPTIME "</th><td>%s</td></tr>"),
    GetUptime().c_str());

  WSContentSend_P(PSTR("<tr><th>" D_FLASH_WRITE_COUNT "</th><td>%d</td></tr>"),
    Settings->save_flag);

  WSContentSend_P(PSTR("<tr><th>" D_BOOT_COUNT "</th><td>%d</td></tr>"),
    Settings->bootcount);

  WSContentSend_P(PSTR("<tr><th>" D_RESTART_REASON "</th><td>%s</td></tr>"),
    GetResetReason().c_str());

  uint32_t maxfn = (TasmotaGlobal.devices_present > MAX_FRIENDLYNAMES) ? MAX_FRIENDLYNAMES : TasmotaGlobal.devices_present;
  for (uint32_t i = 0; i < maxfn; i++) {
    snprintf_P(label, sizeof(label), PSTR(D_FRIENDLY_NAME " %d"), i + 1);
    WSContentSend_P(PSTR("<tr><th>%s</th><td>%s</td></tr>"),
      label,
      SettingsTextEscaped(SET_FRIENDLYNAME1 + i).c_str());
  }

#ifdef CONFIG_ESP_WIFI_REMOTE_ENABLED
  WSContentSend_P(PSTR("<tr><th>" D_HOSTED_MCU "</th><td>%s (%s)</td></tr>"),
    GetHostedMCU().c_str(),
    GetHostedFwVersion(1).c_str());
#endif

  // AP info
  if ((WiFi.getMode() >= WIFI_AP) && (static_cast<uint32_t>(WiFi.softAPIP()) != 0)) {
    WSContentSend_P(PSTR("<tr><th>" D_MAC_ADDRESS " (AP)</th><td>%s</td></tr>"),
      WiFi.softAPmacAddress().c_str());

    WSContentSend_P(PSTR("<tr><th>" D_IP_ADDRESS " (AP)</th><td>%_I</td></tr>"),
      (uint32_t)WiFi.softAPIP());

    WSContentSend_P(PSTR("<tr><th>" D_GATEWAY " (AP)</th><td>%_I</td></tr>"),
      (uint32_t)WiFi.softAPIP());
  }

  // WiFi info
  bool show_hr = false;
  if (Settings->flag4.network_wifi) {
    int32_t rssi = WiFi.RSSI();

    snprintf_P(value, sizeof(value),
      PSTR(D_SSID " %s<br>" D_RSSI " %d%% (%d dBm)<br>" D_MODE " %s<br>" D_CHANNEL " %d<br>" D_BSSID " %s"),
      SettingsTextEscaped(SET_STASSID1 + Settings->sta_active).c_str(),
      WifiGetRssiAsQuality(rssi), rssi,
      WifiGetPhyMode().c_str(),
      WiFi.channel(),
      WiFi.BSSIDstr().c_str());

    snprintf_P(label, sizeof(label), PSTR(D_AP "%d " D_INFORMATION), Settings->sta_active + 1);
    WSContentSend_P(PSTR("<tr><th>%s</th><td>%s</td></tr>"), label, value);

    WSContentSend_P(PSTR("<tr><th>" D_HOSTNAME "</th><td>%s%s</td></tr>"),
      TasmotaGlobal.hostname,
      (Mdns.begun) ? PSTR(".local") : PSTR(""));

#ifdef USE_IPV6
    {
      String ipv6_addr = WifiGetIPv6Str();
      if (ipv6_addr != "") {
        WSContentSend_P(PSTR("<tr><th>IPv6 Global (WiFi)</th><td>%s</td></tr>"), ipv6_addr.c_str());
      }
      ipv6_addr = WifiGetIPv6LinkLocalStr();
      if (ipv6_addr != "") {
        WSContentSend_P(PSTR("<tr><th>IPv6 Local (WiFi)</th><td>%s</td></tr>"), ipv6_addr.c_str());
      }
    }
#endif

    if (static_cast<uint32_t>(WiFi.localIP()) != 0) {
      WSContentSend_P(PSTR("<tr><th>" D_MAC_ADDRESS " (WiFi)</th><td>%s</td></tr>"),
        WiFiHelper::macAddress().c_str());

      WSContentSend_P(PSTR("<tr><th>" D_IP_ADDRESS " (WiFi)</th><td>%_I</td></tr>"),
        (uint32_t)WiFi.localIP());
    }
    show_hr = true;
  }

  if (!TasmotaGlobal.global_state.wifi_down) {
    WSContentSend_P(PSTR("<tr><th>" D_GATEWAY " (WiFi)</th><td>%_I</td></tr>"),
      Settings->ipv4_address[1]);

    WSContentSend_P(PSTR("<tr><th>" D_SUBNET_MASK " (WiFi)</th><td>%_I</td></tr>"),
      Settings->ipv4_address[2]);

#ifdef USE_IPV6
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "1</th><td>%s</td></tr>"),
      DNSGetIPStr(0).c_str());
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "2</th><td>%s</td></tr>"),
      DNSGetIPStr(1).c_str());
#else
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "1</th><td>%_I</td></tr>"),
      Settings->ipv4_address[3]);
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "2</th><td>%_I</td></tr>"),
      Settings->ipv4_address[4]);
#endif
  }

#if defined(USE_ETHERNET)
  if (EthernetHasIP()) {
    WSContentSend_P(PSTR("<tr><th>" D_HOSTNAME " (ETH)</th><td>%s%s</td></tr>"),
      EthernetHostname(),
      (Mdns.begun) ? PSTR(".local") : PSTR(""));

#ifdef USE_IPV6
    {
      String ipv6_eth_addr = EthernetGetIPv6Str();
      if (ipv6_eth_addr != "") {
        WSContentSend_P(PSTR("<tr><th>IPv6 Global (ETH)</th><td>%s</td></tr>"), ipv6_eth_addr.c_str());
      }
      ipv6_eth_addr = EthernetGetIPv6LinkLocalStr();
      if (ipv6_eth_addr != "") {
        WSContentSend_P(PSTR("<tr><th>IPv6 Local (ETH)</th><td>%s</td></tr>"), ipv6_eth_addr.c_str());
      }
    }
#endif

    WSContentSend_P(PSTR("<tr><th>" D_MAC_ADDRESS " (ETH)</th><td>%s</td></tr>"),
      EthernetMacAddress().c_str());

    WSContentSend_P(PSTR("<tr><th>" D_IP_ADDRESS " (ETH)</th><td>%_I</td></tr>"),
      (uint32_t)EthernetLocalIP());
  }

  if (!TasmotaGlobal.global_state.eth_down) {
    WSContentSend_P(PSTR("<tr><th>" D_GATEWAY " (ETH)</th><td>%_I</td></tr>"),
      Settings->eth_ipv4_address[1]);

    WSContentSend_P(PSTR("<tr><th>" D_SUBNET_MASK " (ETH)</th><td>%_I</td></tr>"),
      Settings->eth_ipv4_address[2]);

#ifdef USE_IPV6
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "1 (ETH)</th><td>%s</td></tr>"),
      DNSGetIPStr(0).c_str());
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "2 (ETH)</th><td>%s</td></tr>"),
      DNSGetIPStr(1).c_str());
#else
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "1 (ETH)</th><td>%_I</td></tr>"),
      Settings->eth_ipv4_address[3]);
    WSContentSend_P(PSTR("<tr><th>" D_DNS_SERVER "2 (ETH)</th><td>%_I</td></tr>"),
      Settings->eth_ipv4_address[4]);
#endif
  }
#endif  // USE_ETHERNET

  // HTTP / MQTT / discovery
  WSContentSend_P(PSTR("<tr><th>" D_HTTP_API "</th><td>%s</td></tr>"),
    Settings->flag5.disable_referer_chk ? PSTR(D_ENABLED) : PSTR(D_DISABLED));

  if (Settings->flag.mqtt_enabled) {
    WSContentSend_P(PSTR("<tr><th>" D_MQTT_HOST "</th><td>%s</td></tr>"),
      SettingsTextEscaped(SET_MQTT_HOST).c_str());

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_PORT "</th><td>%d</td></tr>"),
      Settings->mqtt_port);

#ifdef USE_MQTT_TLS
    WSContentSend_P(PSTR("<tr><th>" D_MQTT_TLS_ENABLE "</th><td>%s</td></tr>"),
      Settings->flag4.mqtt_tls ? PSTR(D_ENABLED) : PSTR(D_DISABLED));
#endif

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_USER "</th><td>%s</td></tr>"),
      SettingsTextEscaped(SET_MQTT_USER).c_str());

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_CLIENT "</th><td>%s</td></tr>"),
      TasmotaGlobal.mqtt_client);

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_TOPIC "</th><td>%s</td></tr>"),
      SettingsTextEscaped(SET_MQTT_TOPIC).c_str());

    uint32_t real_index = SET_MQTT_GRP_TOPIC;
    for (uint32_t i = 0; i < MAX_GROUP_TOPICS; i++) {
      if (1 == i) { real_index = SET_MQTT_GRP_TOPIC2 - 1; }
      if (strlen(SettingsText(real_index + i))) {
        snprintf_P(label, sizeof(label), PSTR(D_MQTT_GROUP_TOPIC " %d"), 1 + i);
        WSContentSend_P(PSTR("<tr><th>%s</th><td>%s</td></tr>"),
          label,
          GetGroupTopic_P(stopic, "", real_index + i));
      }
    }

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_FULL_TOPIC "</th><td>%s</td></tr>"),
      GetTopic_P(stopic, CMND, TasmotaGlobal.mqtt_topic, ""));

    WSContentSend_P(PSTR("<tr><th>" D_MQTT " " D_FALLBACK_TOPIC "</th><td>%s</td></tr>"),
      GetFallbackTopic_P(stopic, ""));

    WSContentSend_P(PSTR("<tr><th>" D_MQTT_NO_RETAIN "</th><td>%s</td></tr>"),
      Settings->flag4.mqtt_no_retain ? PSTR(D_ENABLED) : PSTR(D_DISABLED));
  } else {
    WSContentSend_P(PSTR("<tr><th>" D_MQTT "</th><td>" D_DISABLED "</td></tr>"));
  }

#if defined(USE_DISCOVERY)
  WSContentSend_P(PSTR("<tr><th>" D_MDNS_DISCOVERY "</th><td>%s</td></tr>"),
    (Settings->flag3.mdns_enabled) ? PSTR(D_ENABLED) : PSTR(D_DISABLED));

  if (Settings->flag3.mdns_enabled) {
#ifdef WEBSERVER_ADVERTISE
    WSContentSend_P(PSTR("<tr><th>" D_MDNS_ADVERTISE "</th><td>" D_WEB_SERVER "</td></tr>"));
#else
    WSContentSend_P(PSTR("<tr><th>" D_MDNS_ADVERTISE "</th><td>" D_DISABLED "</td></tr>"));
#endif
  }
#endif

  // Hardware / memory
  WSContentSend_P(PSTR("<tr><th>" D_ESP_CHIP_ID "</th><td>%d (%s)</td></tr>"),
    ESP_getChipId(),
    GetDeviceHardwareRevision().c_str());

  {
    uint64_t mac = ESP.getEfuseMac();
    uint8_t b0 = (uint8_t)(mac >> 0);
    uint8_t b1 = (uint8_t)(mac >> 8);
    uint8_t b2 = (uint8_t)(mac >> 16);
    uint8_t b3 = (uint8_t)(mac >> 24);
    uint8_t b4 = (uint8_t)(mac >> 32);
    uint8_t b5 = (uint8_t)(mac >> 40);

    snprintf_P(value, sizeof(value), PSTR("%02X:%02X:%02X:%02X:%02X:%02X"),
      b0, b1, b2, b3, b4, b5);

    WSContentSend_P(PSTR("<tr><th>EFuse MAC</th><td>%s</td></tr>"), value);
  }

  WSContentSend_P(PSTR("<tr><th>" D_FLASH_CHIP_SIZE "</th><td>%d KB</td></tr>"),
    ESP.getFlashChipSize() / 1024);

  WSContentSend_P(PSTR("<tr><th>" D_PROGRAM_FLASH_SIZE "</th><td>%d KB</td></tr>"),
    ESP_getFlashChipMagicSize() / 1024);

  WSContentSend_P(PSTR("<tr><th>" D_PROGRAM_SIZE "</th><td>%d KB</td></tr>"),
    ESP_getSketchSize() / 1024);

  WSContentSend_P(PSTR("<tr><th>" D_FREE_PROGRAM_SPACE "</th><td>%d KB</td></tr>"),
    ESP_getFreeSketchSpace() / 1024);

  dtostrfd(freemem, 1, fmem);
#ifdef USE_GT911
  WSContentSend_P(PSTR("<tr><th>" D_FREE_MEMORY "</th><td>%s KB</td></tr>"), fmem);
#else
  WSContentSend_P(PSTR("<tr><th>" D_FREE_MEMORY "</th><td>%s KB (" D_FRAGMENTATION " %d%%)</td></tr>"),
    fmem,
    ESP_getHeapFragmentation());
#endif

  if (UsePSRAM()) {
    WSContentSend_P(PSTR("<tr><th>" D_PSR_MAX_MEMORY "</th><td>%d KB</td></tr>"),
      ESP.getPsramSize() / 1024);

    WSContentSend_P(PSTR("<tr><th>" D_PSR_FREE_MEMORY "</th><td>%d KB</td></tr>"),
      ESP.getFreePsram() / 1024);
  }

  // Partitions
  {
    uint32_t cur_part = ESP_PARTITION_SUBTYPE_APP_FACTORY;
    const esp_partition_t *running_ota = esp_ota_get_running_partition();
    if (running_ota) { cur_part = running_ota->subtype; }

    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it != NULL; it = esp_partition_next(it)) {
      const esp_partition_t *part = esp_partition_get(it);
      uint32_t part_size = part->size / 1024;

      if (ESP_PARTITION_TYPE_APP == part->type) {
        uint32_t prog_size = 0;

        if (part->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
          prog_size = EspProgramSize(part->label);
        }
        else if ((part->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN) &&
                 (part->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX)) {
          if (cur_part == part->subtype) {
            prog_size = ESP_getSketchSize();
          } else if (cur_part == ESP_PARTITION_SUBTYPE_APP_FACTORY) {
            prog_size = EspProgramSize(part->label);
          }
        }

        snprintf_P(label, sizeof(label), PSTR(D_PARTITION " %s%s"),
          part->label,
          (part->subtype == cur_part) ? "*" : "");

        if (prog_size) {
          uint32_t part_used = ((prog_size / 1024) * 100) / part_size;
          snprintf_P(value, sizeof(value), PSTR("%d KB (" D_USED " %d%%)"), part_size, part_used);
        } else {
          snprintf_P(value, sizeof(value), PSTR("%d KB"), part_size);
        }

        WSContentSend_P(PSTR("<tr><th>%s</th><td>%s</td></tr>"), label, value);
      }

      if ((ESP_PARTITION_TYPE_DATA == part->type) && (ESP_PARTITION_SUBTYPE_DATA_SPIFFS == part->subtype)) {
        snprintf_P(label, sizeof(label), PSTR(D_PARTITION " fs"));
        snprintf_P(value, sizeof(value), PSTR("%d KB"), part_size);
        WSContentSend_P(PSTR("<tr><th>%s</th><td>%s</td></tr>"), label, value);
      }
    }
    esp_partition_iterator_release(it);
  }

  WSContentSend_P(PSTR("</table>"));
  WSContentCardEnd();
  WSContentStop();
}

/*********************************************************************************************\
 * HandleUpgradeFirmware
\*********************************************************************************************/

#if defined(USE_ZIGBEE_EZSP) || defined(USE_TASMOTA_CLIENT) || defined(USE_RF_FLASH) || defined(USE_CCLOADER)
#define USE_WEB_FW_UPGRADE
#endif

#ifdef USE_WEB_FW_UPGRADE

struct {
  size_t spi_hex_size;
  size_t spi_sector_counter;
  size_t spi_sector_cursor;
  bool active;
  bool ready;
} BUpload;

/*-------------------------------------------------------------------------------------------*/

void BUploadInit(uint32_t file_type) {
  Web.upload_file_type = file_type;
  BUpload.spi_hex_size = 0;
  BUpload.spi_sector_counter = FlashWriteStartSector();
  BUpload.spi_sector_cursor = 0;
  BUpload.active = true;
  BUpload.ready = false;
}

/*-------------------------------------------------------------------------------------------*/

uint32_t BUploadWriteBuffer(uint8_t *buf, size_t size) {
  if (0 == BUpload.spi_sector_cursor) { // Starting a new sector write so we need to erase it first
    if (!ESP.flashEraseSector(BUpload.spi_sector_counter)) {
      return 7;  // Upload aborted - flash failed
    }
  }
  BUpload.spi_sector_cursor++;
  if (!ESP.flashWrite((BUpload.spi_sector_counter * SPI_FLASH_SEC_SIZE) + ((BUpload.spi_sector_cursor -1) * HTTP_UPLOAD_BUFLEN), (uint32_t*)buf, size)) {
    return 7;  // Upload aborted - flash failed
  }
  BUpload.spi_hex_size += size;
  if (2 == BUpload.spi_sector_cursor) {  // The web upload sends 2048 bytes at a time so keep track of the cursor position to reset it for the next flash sector erase
    BUpload.spi_sector_cursor = 0;
    BUpload.spi_sector_counter++;
    if (BUpload.spi_sector_counter > FlashWriteMaxSector()) {
      return 9;  // File too large - Not enough free space
    }
  }
  return 0;
}

#endif  // USE_WEB_FW_UPGRADE

/*-------------------------------------------------------------------------------------------*/

void HandleUpgradeFirmware(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_FIRMWARE_UPGRADE));

  WSContentStart_P(PSTR(D_FIRMWARE_UPGRADE));
  WSContentSendStyle();
  WSSendPageScripts(WS_SCRIPT_UPLOAD);
  WSContentPageHeader(PSTR(D_FIRMWARE_UPGRADE), PSTR("Update firmware using OTA URL or a local file upload."));

  WSContentCardStart(PSTR(D_UPGRADE_BY_WEBSERVER), nullptr);
  WSContentSend_P(HTTP_FORM_UPG_CARD, SettingsTextEscaped(SET_OTAURL).c_str());
  WSContentCardEnd();

  WSContentCardStart(PSTR(D_UPGRADE_BY_FILE_UPLOAD), nullptr);
  WSContentSend_P(HTTP_DIV_F1_BLOCK);
  WSContentSend_P(PSTR("<div class='ts-note'>Upload a firmware image directly from your device.</div>"));

  if (EspSingleOtaPartition() && !EspRunningFactoryPartition()) {
    WSContentSend_P(HTTP_FORM_RST_UPG_FCT_CARD, PSTR(D_START_UPGRADE));
  } else {
    WSContentSend_P(HTTP_FORM_RST_UPG_CARD, PSTR(D_START_UPGRADE));
  }

  WSContentCardEnd();


  WSContentStop();

  Web.upload_file_type = UPL_TASMOTA;
}

/*-------------------------------------------------------------------------------------------*/

void HandleUpgradeFirmwareStart(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  char command[TOPSZ + 10];  // OtaUrl

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_UPGRADE_STARTED));
  WifiConfigCounter();

  char otaurl[TOPSZ];
  WebGetArg(PSTR("o"), otaurl, sizeof(otaurl));
  if (strlen(otaurl)) {
    snprintf_P(command, sizeof(command), PSTR(D_CMND_OTAURL " %s"), otaurl);
    ExecuteWebCommand(command);
  }

  WSContentStart_P(PSTR(D_INFORMATION));
  WSContentSendStyle();
  WSSendPageScripts(WS_SCRIPT_RELOAD, HTTP_OTA_RESTART_RECONNECT_TIME);
  WSContentSend_P(PSTR("<div style='text-align:center;'><b>" D_UPGRADE_STARTED " ...</b></div>"));
  WSContentSend_P(HTTP_MSG_RSTRT);
  WSContentStop();

  snprintf_P(command, sizeof(command), PSTR(D_CMND_UPGRADE " 1"));
  ExecuteWebCommand(command);
}

/*-------------------------------------------------------------------------------------------*/

void HandleUploadDone(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

#if defined(USE_ZIGBEE_EZSP)
  if ((UPL_EFR32 == Web.upload_file_type) && !Web.upload_error && BUpload.ready) {
    BUpload.ready = false;  //  Make sure not to follow thru again
    // GUI xmodem
    ZigbeeUploadStep1Done(FlashWriteStartSector(), BUpload.spi_hex_size);
    HandleZigbeeXfer();
    return;
  }
#endif  // USE_ZIGBEE_EZSP

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_UPLOAD_DONE));

  WifiConfigCounter();
  UploadServices(1);

  WSContentStart_P(PSTR(D_INFORMATION));
  WSContentSendStyle();
  if (!Web.upload_error) {
    WSSendPageScripts(WS_SCRIPT_RELOAD,
      (UPL_TASMOTA == Web.upload_file_type) ? HTTP_OTA_RESTART_RECONNECT_TIME : HTTP_RESTART_RECONNECT_TIME);
  }
  WSContentSend_P(PSTR("<div style='text-align:center;'><b>" D_UPLOAD " <font color='#"));
  if (Web.upload_error) {
    WSContentSend_P(PSTR("%06x'>" D_FAILED "</font></b><br><br>"), WebColor(COL_TEXT_WARNING));
    char error[100];
    if (Web.upload_error < 10) {
      GetTextIndexed(error, sizeof(error), Web.upload_error -1, kUploadErrors);
    } else {
      snprintf_P(error, sizeof(error), PSTR(D_UPLOAD_ERROR_CODE " %d"), Web.upload_error);
    }
    WSContentSend_P(error);
    DEBUG_CORE_LOG(PSTR("UPL: %s"), error);
    TasmotaGlobal.stop_flash_rotate = Settings->flag.stop_flash_rotate;  // SetOption12 - Switch between dynamic or fixed slot flash save location
    Web.upload_error = 0;
  } else {
    WSContentSend_P(PSTR("%06x'>" D_SUCCESSFUL "</font></b><br>"), WebColor(COL_TEXT_SUCCESS));
    TasmotaGlobal.restart_flag = 2;  // Always restart to re-enable disabled features during update
    WSContentSend_P(HTTP_MSG_RSTRT);
    ShowWebSource(SRC_WEBGUI);
  }
  SettingsBufferFree();
  WSContentSend_P(PSTR("</div><br>"));
  WSContentStop();
}

/*-------------------------------------------------------------------------------------------*/

void UploadServices(uint32_t start_service) {
  if (Web.upload_services_stopped != start_service) { return; }
  Web.upload_services_stopped = !start_service;

  if (start_service) {
//    AddLog(LOG_LEVEL_DEBUG, PSTR("UPL: Services enabled"));

/*
    MqttRetryCounter(0);
*/
    AllowInterrupts(1);
  } else {
//    AddLog(LOG_LEVEL_DEBUG, PSTR("UPL: Services disabled"));

    AllowInterrupts(0);
/*
    MqttRetryCounter(60);
    if (Settings->flag.mqtt_enabled) {  // SetOption3 - Enable MQTT
      MqttDisconnect();
    }
*/
  }
}

/*-------------------------------------------------------------------------------------------*/

void HandleUploadLoop(void) {
  // Based on ESP8266HTTPUpdateServer.cpp uses ESP8266WebServer Parsing.cpp and Cores Updater.cpp (Update)
  static uint32_t upload_size;
  static bool upload_error_signalled;

  if (HTTP_USER == Web.state) { return; }

  if (Web.upload_error) {
    if (!upload_error_signalled) {
      if (UPL_TASMOTA == Web.upload_file_type) { Update.end(); }
      UploadServices(1);

//      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_UPLOAD "Upload error %d"), Web.upload_error);

      upload_error_signalled = true;
    }
    return;
  }

  HTTPUpload& upload = Webserver->upload();

  // ***** Step1: Start upload file
  if (UPLOAD_FILE_START == upload.status) {
    Web.upload_error = 0;
    upload_error_signalled = false;
    char tmp[16];
    
    WebGetArg("fsz", tmp, sizeof(tmp));                    // filesize
    upload_size = (!strlen(tmp)) ? 0 : atoi(tmp);

    UploadServices(0);

    if (0 == upload.filename.c_str()[0]) {
      Web.upload_error = 1;  // No file selected
      return;
    }

    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_UPLOAD D_FILE " %s (%d bytes)"), upload.filename.c_str(), upload_size);

#ifdef USE_UFILESYS
    if (UPL_UFSFILE == Web.upload_file_type) {
      const uint32_t freeBytes = (UfsFree() * 1024);
      if (upload_size > freeBytes) {
        Web.upload_error = 9;   // File too large
        return;
      }

      if (!UfsUploadFileOpen(upload.filename.c_str())) {
        Web.upload_error = 2;   // Not enough space
        return;
      }
    }
#endif  // USE_UFILESYS

    SettingsSave(1);  // Free flash for upload
  }

  // ***** Step2: Write upload file
  else if (UPLOAD_FILE_WRITE == upload.status) {
    if (0 == upload.totalSize) {  // First block received
      if (UPL_SETTINGS == Web.upload_file_type) {
        uint32_t set_size = sizeof(TSettings);
#ifdef USE_UFILESYS
        if (('s' == upload.buf[2]) && ('e' == upload.buf[3])) {  // /.settings
          set_size = upload.buf[14] + (upload.buf[15] << 8);
        }
#endif  // USE_UFILESYS
        if (!SettingsBufferAlloc(set_size)) {
          Web.upload_error = 2;  // Not enough space
          return;
        }
        Web.config_block_count = 0;
      }
#ifdef USE_WEB_FW_UPGRADE
#ifdef USE_RF_FLASH
      else if ((SONOFF_BRIDGE == TasmotaGlobal.module_type) && (':' == upload.buf[0])) {  // Check if this is a RF bridge FW file
        BUploadInit(UPL_EFM8BB1);
      }
#endif  // USE_RF_FLASH
#ifdef USE_TASMOTA_CLIENT
      else if (TasmotaClient_Available() && (':' == upload.buf[0])) {  // Check if this is a ARDUINO CLIENT hex file
        BUploadInit(UPL_TASMOTACLIENT);
      }
#endif  // USE_TASMOTA_CLIENT

#ifdef USE_CCLOADER
      else if (CCLChipFound() && 0x02 == upload.buf[0]) { // the 0x02 is only an assumption!!
        BUploadInit(UPL_CCL);
      }
#endif  // USE_CCLOADER
#ifdef USE_ZIGBEE_EZSP


      else if (PinUsed(GPIO_ZIGBEE_RX) && PinUsed(GPIO_ZIGBEE_TX) && (0xEB == upload.buf[0])) {  // Check if this is a Zigbee bridge FW file

        // Read complete file into ESP8266 flash
        // Current files are about 200k
        Web.upload_error = ZigbeeUploadStep1Init();  // 1
        if (Web.upload_error != 0) { return; }
        BUploadInit(UPL_EFR32);
      }
#endif  // USE_ZIGBEE_EZSP
#endif  // USE_WEB_FW_UPGRADE
      else if (UPL_TASMOTA == Web.upload_file_type) {
        if ((upload.buf[0] != 0xE9) && (upload.buf[0] != 0x1F)) {  // 0x1F is gzipped 0xE9
          Web.upload_error = 3;      // Invalid file signature - Magic byte is not 0xE9
          return;
        }
        if (0xE9 == upload.buf[0]) {

          char tmp[16];
          WebGetArg("fsz", tmp, sizeof(tmp));                    // filesize
          uint32_t upload_size = (!strlen(tmp)) ? 0 : atoi(tmp);
          AddLog(LOG_LEVEL_DEBUG, D_LOG_UPLOAD "Freespace %i Filesize %i", ESP.getFreeSketchSpace(), upload_size);
          if (upload_size > ESP.getFreeSketchSpace()) {   // TODO revisit this test
            Web.upload_error = 4;  // Program flash size is larger than real flash size
            return;
          }
  //            upload.buf[2] = 3;  // Force DOUT - ESP8285
        }
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {         //start with max available size
          Web.upload_error = 2;  // Not enough space
          return;
        }
      }
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_UPLOAD "File type %d"), Web.upload_file_type);
    }  // First block received

    if (UPL_SETTINGS == Web.upload_file_type) {
      if (upload.currentSize > (settings_size - (Web.config_block_count * HTTP_UPLOAD_BUFLEN))) {
        Web.upload_error = 9;  // File too large
        return;
      }
      memcpy(settings_buffer + (Web.config_block_count * HTTP_UPLOAD_BUFLEN), upload.buf, upload.currentSize);
      Web.config_block_count++;
    }
#ifdef USE_UFILESYS
    else if (!Web.upload_error && UPL_UFSFILE == Web.upload_file_type) {
      if (!UfsUploadFileWrite(upload.buf, upload.currentSize)) {
        Web.upload_error = 9;  // File too large
        return;
      }
    }
#endif  // USE_UFILESYS
#ifdef USE_WEB_FW_UPGRADE
    else if (BUpload.active) {
      // Write a block
//      AddLog(LOG_LEVEL_DEBUG, PSTR("DBG: Size %d, Data '%32_H'"), upload.currentSize, upload.buf);
      Web.upload_error = BUploadWriteBuffer(upload.buf, upload.currentSize);
      if (Web.upload_error != 0) { return; }
    }
#endif  // USE_WEB_FW_UPGRADE
    else if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
//      Web.upload_error = 5;  // Upload buffer miscompare
      Web.upload_error = 2;  // Not enough space
      return;
    }
    if (upload.totalSize && !(upload.totalSize % 102400)) {
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_UPLOAD "Progress %d KB"), upload.totalSize / 1024);
    }
  }

  // ***** Step3: Finish upload file
  else if (UPLOAD_FILE_END == upload.status) {
    UploadServices(1);
    if (UPL_SETTINGS == Web.upload_file_type) {
      if (!SettingsConfigRestore()) {
        Web.upload_error = 8;  // File invalid
        return;
      }
    }
#ifdef USE_UFILESYS
    else if (!Web.upload_error && UPL_UFSFILE == Web.upload_file_type) {
      UfsUploadFileClose();
    }
#endif  // USE_UFILESYS
#ifdef USE_WEB_FW_UPGRADE
    else if (BUpload.active) {
      // Done writing the data to SPI flash
      BUpload.active = false;

      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_UPLOAD "Transfer %u bytes"), upload.totalSize);

      uint8_t* data = FlashDirectAccess();

//      uint32_t* values = (uint32_t*)(data);  // Only 4-byte access allowed
//      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_UPLOAD "Head 0x%08X"), values[0]);

      uint32_t error = 0;
#ifdef USE_RF_FLASH
      if (UPL_EFM8BB1 == Web.upload_file_type) {
        error = SnfBrUpdateFirmware(data, BUpload.spi_hex_size);
      }
#endif  // USE_RF_FLASH
#ifdef USE_TASMOTA_CLIENT
      if (UPL_TASMOTACLIENT == Web.upload_file_type) {
        error = TasmotaClient_Flash(data, BUpload.spi_hex_size);
      }
#endif  // USE_TASMOTA_CLIENT

#ifdef USE_CCLOADER
      if (UPL_CCL == Web.upload_file_type) {
        error = CLLFlashFirmware(data, BUpload.spi_hex_size);
      }
#endif  
#ifdef USE_ZIGBEE_EZSP
      if (UPL_EFR32 == Web.upload_file_type) {
        BUpload.ready = true;  // So we know on upload success page if it needs to flash hex or do a normal restart
      }
#endif  // USE_ZIGBEE_EZSP
      if (error != 0) {
//        AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_UPLOAD "Transfer error %d"), error);
        Web.upload_error = error + (100 * (Web.upload_file_type -1));  // Add offset to discriminate transfer errors
        return;
      }
    }
#endif  // USE_WEB_FW_UPGRADE
    else if (!Update.end(true)) { // true to set the size to the current progress
      Web.upload_error = 6;  // Upload failed. Enable logging 3
      return;
    }
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_UPLOAD D_SUCCESSFUL " %u bytes"), upload.totalSize);
  }

  // ***** Step4: Abort upload file
  else {
    UploadServices(1);
    Web.upload_error = 7;  // Upload aborted
    if (UPL_TASMOTA == Web.upload_file_type) { Update.end(); }
  }
  // do actually wait a little to allow ESP32 tasks to tick
  // fixes task timeout in ESP32Solo1 style unicore code.
  delay(10);
  OsWatchLoop();
//  Scheduler();          // Feed OsWatch timer to prevent restart on long uploads
}

/*********************************************************************************************\
 * HandlePreflightRequest
\*********************************************************************************************/

void HandlePreflightRequest(void) {
#ifdef USE_CORS
  HttpHeaderCors();
#endif
  Webserver->sendHeader("Access-Control-Allow-Methods", "GET, POST");
  Webserver->sendHeader("Access-Control-Allow-Headers", "authorization");
  WSSend(200, CT_HTML, "");
}

/*********************************************************************************************\
 * HandleSwitchBootPartition
\*********************************************************************************************/

// Switch boot partition
//
// Parameter `u4` is either `fct` or `ota` to switch to factory or ota
// If not in single-OTA mode
//
// The page can return the followinf (code 200)
// `false`: the current partition is not the target, but a restart to factory is triggered (polling required)
// `true`: the current partition is the one required
// `none`: there is no factory partition

// return a simple status page as text/plain code 200
static void WSReturnSimpleString(const char *msg) {
  if (nullptr == msg) { msg = ""; }
  Webserver->client().flush();
  WSHeaderSend();
  Webserver->send(200, "text/plain", msg);
}

/*-------------------------------------------------------------------------------------------*/

void HandleSwitchBootPartition(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  char tmp1[8];
  WebGetArg(PSTR("u4"), tmp1, sizeof(tmp1));

  bool switch_factory = false;      // trigger a restart to factory partition?
  bool switch_ota = false;          // switch back to OTA partition
  bool single_ota = EspSingleOtaPartition();
  bool api_mode = Webserver->hasArg("api");   // api-mode, returns `true`, `false` or `none`

  // switch to next OTA?
  if (strcmp("ota", tmp1) == 0) {
    switch_ota = true;
    if (single_ota && !EspRunningFactoryPartition()) {
      switch_ota = false;                     // if in single-OTA and already running OTA, nothing to do
    }
  }
  // switch to factory ?
  if (strcmp("fct", tmp1) == 0 && single_ota && !EspRunningFactoryPartition()) {
    switch_factory = true;
  }

  // apply the change in flash and return result
  if (switch_factory || switch_ota) {
    XsnsXdrvCall(FUNC_ABOUT_TO_RESTART);
    SettingsSaveAll();
    if (switch_factory) {
      EspPrepRestartToSafeBoot();
    } else {
      const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
      esp_ota_set_boot_partition(partition);
    }

    if (api_mode) {
      WSReturnSimpleString("false");
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_RESTART));
      EspRestart();
    } else {
      WebRestart(4);
    }
  } else {
    if (api_mode) {
      // return `none` or `true`
      WSReturnSimpleString(EspSingleOtaPartition() ? "true" : "none");
    } else {
      Webserver->sendHeader("Location", "/", true);
      Webserver->send(302, "text/plain", "");
    }
  }
  Web.upload_file_type = UPL_TASMOTA;
}

/*********************************************************************************************\
 * HandleHttpCommand
\*********************************************************************************************/

void HandleHttpCommand(void) {
  if (!HttpCheckPriviledgedAccess(false)) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_COMMAND));

  if (!WebAuthenticate()) {
    // Prefer authorization via HTTP header (Basic auth), if it fails, use legacy method via GET parameters
    char tmp1[33];
    WebGetArg(PSTR("user"), tmp1, sizeof(tmp1));
    char tmp2[strlen(SettingsText(SET_WEBPWD)) + 2];  // Need space for an entered password longer than set password
    WebGetArg(PSTR("password"), tmp2, sizeof(tmp2));

    if (!(!strcmp(tmp1, WEB_USERNAME) && !strcmp(tmp2, SettingsText(SET_WEBPWD)))) {
      WSContentBegin(401, CT_APP_JSON);
      WSContentSend_P(PSTR("{\"" D_RSLT_WARNING "\":\"" D_NEED_USER_AND_PASSWORD "\"}"));
      WSContentEnd();

      // https://github.com/arendst/Tasmota/discussions/15420
      ShowWebSource(SRC_WEBCOMMAND);
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP "Bad userid and/or password"));

      return;
    }
  }

  WSContentBegin(200, CT_APP_JSON);
  String svalue = Webserver->arg(F("cmnd"));
  if (svalue.length() && (svalue.length() < MQTT_MAX_PACKET_SIZE)) {
    uint32_t curridx = TasmotaGlobal.log_buffer_pointer;
    TasmotaGlobal.templog_level = LOG_LEVEL_INFO;
    ExecuteWebCommand((char*)svalue.c_str(), SRC_WEBCOMMAND);
    WSContentSend_P(PSTR("{"));
    bool cflg = false;
    uint32_t index = curridx;
    char* line;
    size_t len;
    while (GetLog(TasmotaGlobal.templog_level, &index, &line, &len)) {
      // [14:49:36.123 MQTT: stat/wemos5/RESULT = {"POWER":"OFF"}] > [{"POWER":"OFF"}]
      char* JSON = (char*)memchr(line, '{', len);
      if (JSON) {  // Is it a JSON message (and not only [15:26:08 MQT: stat/wemos5/POWER = O])
        if (cflg) { WSContentSend_P(PSTR(",")); }
        uint32_t JSONlen = len - (JSON - line) -3;
        for( ++JSON ; JSONlen && JSON[JSONlen] != '}' ; JSONlen-- );
        WSContentSend(JSON, JSONlen);
        cflg = true;
      }
    }
    WSContentSend_P(PSTR("}"));
    TasmotaGlobal.templog_level = 0;
  } else {
    WSContentSend_P(PSTR("{\"" D_RSLT_WARNING "\":\"" D_ENTER_COMMAND " cmnd=\"}"));
  }
  WSContentEnd();
}

/*********************************************************************************************\
 * HandleManagement
\*********************************************************************************************/

void HandleManagement(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_MANAGEMENT));

  WSContentStart_P(PSTR(D_MANAGEMENT));
  WSContentSendStyle();
  WSContentPageHeader(PSTR(D_MANAGEMENT), PSTR("Maintenance extensions and driver-specific actions."));

  XdrvMailbox.index = 0;
  XdrvXsnsCall(FUNC_WEB_ADD_CONSOLE_BUTTON);
  XdrvCall(FUNC_WEB_ADD_MANAGEMENT_BUTTON);

  WSContentStop();
}
/*********************************************************************************************\
 * HandleConsole
\*********************************************************************************************/

void HandleConsole(void) {
  if (!HttpCheckPriviledgedAccess()) { return; }

  if (Webserver->hasArg(F("c2"))) {      // Console refresh requested
    HandleConsoleRefresh();
    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_CONSOLE));

  WSContentStart_P(PSTR(D_CONSOLE));
  WSContentSendStyle_P(HTTP_CMND_STYLE);
  WSSendPageScripts(WS_SCRIPT_CONSOLE);

  WSContentSend_P(HTTP_FORM_CMND);
  WSContentStop();
}

/*-------------------------------------------------------------------------------------------*/

void HandleConsoleRefresh(void) {
  String svalue = Webserver->arg(F("c1"));
  if (svalue.length() && (svalue.length() < MQTT_MAX_PACKET_SIZE)) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_COMMAND "%s"), svalue.c_str());
    ExecuteWebCommand((char*)svalue.c_str(), SRC_WEBCONSOLE);
  }

  char stmp[8];
  WebGetArg(PSTR("c2"), stmp, sizeof(stmp));
  uint32_t index = 0;                // Initial start, dump all
  if (strlen(stmp)) { index = atoi(stmp); }

  WSContentBegin(200, CT_PLAIN);
  WSContentSend_P(PSTR("%d}1%d}1"), TasmotaGlobal.log_buffer_pointer, Web.reset_web_log_flag);
  if (!Web.reset_web_log_flag) {
    index = 0;
    Web.reset_web_log_flag = true;
  }
  Web.cflg = (index);
  char* line;
  size_t len;
  while (GetLog(Settings->weblog_level, &index, &line, &len)) {
    if (!LogDataJsonPrettyPrint(line, len -1, WSContentSendLDJsonPPCb)) {
      WSContentSendLDJsonPPCb(line, len -1);
    }
  }
  WSContentSend_P(PSTR("}1"));
  WSContentEnd();
}

void WSContentSendLDJsonPPCb(const char* line, uint32_t len) {
  if (Web.cflg) { WSContentSend_P(PSTR("\n")); }
  WSContentSend(line, len);
  Web.cflg = true;
}

/********************************************************************************************/

void HandleNotFound(void) {
//  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP "Not found (%s)"), Webserver->uri().c_str());
#ifndef NO_CAPTIVE_PORTAL
  if (CaptivePortal()) { return; }  // If captive portal redirect instead of displaying the error page.
#endif  // NO_CAPTIVE_PORTAL

  {
    WSContentBegin(404, CT_PLAIN);
    WSContentSend_P(PSTR(D_FILE_NOT_FOUND "\n\nURI: %s\nMethod: %s\nArguments: %d\n"), Webserver->uri().c_str(), (Webserver->method() == HTTP_GET) ? PSTR("GET") : PSTR("POST"), Webserver->args());
    for (uint32_t i = 0; i < Webserver->args(); i++) {
      WSContentSend_P(PSTR(" %s: %s\n"), Webserver->argName(i).c_str(), Webserver->arg(i).c_str());
    }
    WSContentEnd();
  }
}

/********************************************************************************************/

#ifndef NO_CAPTIVE_PORTAL
/* Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
bool CaptivePortal(void) {
  // Possible hostHeader: connectivitycheck.gstatic.com or 192.168.4.1
  if ((WifiIsInManagerMode()) && !ValidIpAddress(Webserver->hostHeader().c_str())) {
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_HTTP D_REDIRECTED));

    Webserver->sendHeader(F("Location"), String(F("http://")) + IPGetListeningAddressStr(), true);
    WSSend(302, CT_PLAIN, "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
    Webserver->client().stop();  // Stop is needed because we sent no content length
    return true;
  }
  return false;
}
#endif  // NO_CAPTIVE_PORTAL

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/


enum {QUERY_DEFAULT=0, QUERY_RUN};
int WebQuery(char *buffer, int query_function);

#ifdef USE_WEBRUN
/*-------------------------------------------------------------------------------------------*/

char *WebRunBuffer = nullptr;
char *WebRunContext = nullptr;
bool WebRunMutex = false;

void WebRunLoop(void) {
  if (WebRunBuffer && !WebRunMutex && BACKLOG_EMPTY) {
    WebRunMutex = true;
    char *command = strtok_r(WebRunContext, "\n\r", &WebRunContext);
    if (command) {
      while (isspace(*command)) command++; // skip space
      if (*command && ';' != *command)
        ExecuteCommand(command, SRC_WEB);
    } else {
      free(WebRunBuffer);
      WebRunBuffer = WebRunContext = nullptr;
    }
    WebRunMutex = false;
  }
}

/*-------------------------------------------------------------------------------------------*/

void WebRunInit(const char *command_buffer) {
  if (!WebRunBuffer) {
    int len = strlen(command_buffer);
    WebRunContext = WebRunBuffer = (char*)malloc(len+1);
    if (WebRunBuffer) {
      memcpy(WebRunBuffer, command_buffer, len);
      WebRunBuffer[len] = 0;
    } else {
      AddLog(LOG_LEVEL_DEBUG, PSTR("WEBRUN: not enough memory"));
    }
  } else {
    AddLog(LOG_LEVEL_DEBUG, PSTR("WEBRUN: previous not completed"));
  }
}
#endif // #ifdef USE_WEBRUN

/*-------------------------------------------------------------------------------------------*/

int WebQuery(char *buffer, int query_function = 0) {
  // http://192.168.1.1/path GET                                         -> Sends HTTP GET http://192.168.1.1/path
  // http://192.168.1.1/path POST {"some":"message"}                     -> Sends HTTP POST to http://192.168.1.1/path with body {"some":"message"}
  // http://192.168.1.1/path PUT [Autorization: Bearer abcdxyz] potato   -> Sends HTTP PUT to http://192.168.1.1/path with authorization header and body "potato"
  // http://192.168.1.1/path PATCH patchInfo                             -> Sends HTTP PATCH to http://192.168.1.1/path with body "potato"

  // Valid HTTP Commands: GET, POST, PUT, and PATCH
  // An unlimited number of headers can be sent per request, and a body can be sent for all command types
  // The body will be ignored if sending a GET command

#if defined(USE_WEBCLIENT_HTTPS)
  HTTPClientLight http;
#else // HTTP only
  WiFiClient http_client;
  HTTPClient http;
#endif

  int status = WEBCMND_WRONG_PARAMETERS;

  char *temp;
  const char *url = strtok_r(buffer, " ", &temp);
  const char *method = strtok_r(temp, " ", &temp);

  if (url) {
#if defined(USE_WEBCLIENT_HTTPS)
    if (http.begin(UrlEncode(url))) {
#else // HTTP only
    if (http.begin(http_client, UrlEncode(url))) {
#endif
      char empty_body[1] = { 0 };
      char *body = empty_body;
      if (temp) {                             // There is a body and/or header
        if (temp[0] == '[') {                 // Header information was sent; decode it
          temp += 1;
          temp = strtok_r(temp, "]", &body);
          bool headerFound = true;
          while (headerFound) {
            char *header = strtok_r(temp, ":", &temp);
            if (header) {
              char *headerBody = strtok_r(temp, "|", &temp);
              if (headerBody) {
                http.addHeader(header, headerBody);
              }
              else headerFound = false;
            }
            else headerFound = false;
          }
        } else {                              // No header information was sent, but there was a body
          body = temp;
        }
      }

      int http_code;
      if ((!method) || 0 == strcasecmp_P(method, PSTR("GET"))) { http_code = http.GET(); }
      else if (0 == strcasecmp_P(method, PSTR("POST"))) { http_code = http.POST(body); }
      else if (0 == strcasecmp_P(method, PSTR("PUT"))) { http_code = http.PUT(body); }
      else if (0 == strcasecmp_P(method, PSTR("PATCH"))) { http_code = http.PATCH(body); }
      else return status;

      if (http_code > 0) {                    // http_code will be negative on error
#if defined(USE_WEBSEND_RESPONSE) || defined(USE_WEBRUN)
        if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_MOVED_PERMANENTLY) {
          // Return received data to the user - Adds 900+ bytes to the code
          String response = http.getString(); // File found at server - may need lot of ram or trigger out of memory!
          const char* read = response.c_str();
//          uint32_t len = response.length() + 1;
//          AddLog(LOG_LEVEL_DEBUG, PSTR("DBG: Response '%*_H' = %s"), len, (uint8_t*)read, read);
#ifdef USE_WEBRUN
          if (QUERY_RUN == query_function)
            WebRunInit(read);
#endif
#ifdef USE_WEBSEND_RESPONSE
          char text[3] = { 0 };               // Make room foor double %
          text[0] = *read++;
          if (text[0] != '\0') {
            Response_P(PSTR("{\"" D_CMND_WEBQUERY "\":"));
            bool assume_json = (text[0] == '{') || (text[0] == '[');
            if (!assume_json) { ResponseAppend_P(PSTR("\"")); }
            while (text[0] != '\0') {
              if (text[0] > 31) {             // Remove control characters like linefeed
                if ('%' == text[0]) {         // Fix char string formatting for %
                  text[1] = '%';
                }
                if (assume_json) {
                  if (ResponseAppend_P(text) == ResponseSize()) { break; };
                } else {
                  if (ResponseAppend_P(EscapeJSONString(text).c_str()) == ResponseSize()) { break; };
                }
              }
              text[0] = *read++;
              text[1] = '\0';
            }
            if (!assume_json) { ResponseAppend_P(PSTR("\"")); }
            ResponseJsonEnd();
            status = WEBCMND_VALID_RESPONSE;
          } else {
#endif  // USE_WEBSEND_RESPONSE
            status = WEBCMND_DONE;
          }
        } else
#endif  // USE_WEBSEND_RESPONSE || USE_WEBRUN
        status = WEBCMND_DONE;
      } else {
        status = WEBCMND_CONNECT_FAILED;
      }
      http.end();                             // Clean up connection data
    } else {
      status = WEBCMND_HOST_NOT_FOUND;
    }
  }
  return status;
}


/*-------------------------------------------------------------------------------------------*/

const char kWebCmndStatus[] PROGMEM = D_JSON_DONE "|" D_JSON_WRONG_PARAMETERS "|" D_JSON_CONNECT_FAILED "|" D_JSON_HOST_NOT_FOUND "|" D_JSON_MEMORY_ERROR "|" 
#ifdef USE_WEBGETCONFIG
  "|" D_JSON_FILE_NOT_FOUND "|" D_JSON_OTHER_HTTP_ERROR "|" D_JSON_CONNECTION_LOST "|" D_JSON_INVALID_FILE_TYPE
#endif // USE_WEBGETCONFIG
;

const char kWebCommands[] PROGMEM = "|"  // No prefix
  D_CMND_WEBLOG "|"
  D_CMND_WEBTIME "|"
#ifdef USE_SENDMAIL
  D_CMND_SENDMAIL "|"
#endif
  D_CMND_WEBSERVER "|" D_CMND_WEBPASSWORD "|" D_CMND_WEBREFRESH "|" D_CMND_WEBSEND "|" D_CMND_WEBQUERY "|"
  D_CMND_WEBCOLOR "|" D_CMND_WEBSENSOR "|" D_CMND_WEBBUTTON "|" D_CMND_WEBCANVAS
#ifdef USE_WEBGETCONFIG
  "|" D_CMND_WEBGETCONFIG
#endif
#ifdef USE_WEBRUN
  "|" D_CMND_WEBRUN
#endif
#ifdef USE_CORS
  "|" D_CMND_CORS
#endif
;

void (* const WebCommand[])(void) PROGMEM = {
  &CmndWeblog,
  &CmndWebTime,
#ifdef USE_SENDMAIL
  &CmndSendmail,
#endif
  &CmndWebServer, &CmndWebPassword, &CmndWebRefresh, &CmndWebSend, &CmndWebQuery,
  &CmndWebColor, &CmndWebSensor, &CmndWebButton, &CmndWebCanvas
#ifdef USE_WEBGETCONFIG
  , &CmndWebGetConfig
#endif
#ifdef USE_WEBRUN
  , &CmndWebRun
#endif
#ifdef USE_CORS
  , &CmndCors
#endif
  };

/*********************************************************************************************/

void CmndWebTime(void) {
  // 2017-03-07T11:08:02-07:00
  // 0123456789012345678901234
  //
  // WebTime 0,16  = 2017-03-07T11:08 - No seconds
  // WebTime 11,19 = 11:08:02
  uint32_t values[2] = { 0 };
  String datetime = GetDateAndTime(DT_LOCAL);
  if (ParseParameters(2, values) > 1) {
    Settings->web_time_start = values[0];
    Settings->web_time_end = values[1];
    if (Settings->web_time_end > datetime.length()) { Settings->web_time_end = datetime.length(); }
    if (Settings->web_time_start >= Settings->web_time_end) { Settings->web_time_start = 0; }
  }
  Response_P(PSTR("{\"%s\":[%d,%d],\"Time\":\"%s\"}"),
    XdrvMailbox.command, Settings->web_time_start, Settings->web_time_end,
    datetime.substring(Settings->web_time_start, Settings->web_time_end).c_str());
}


#ifdef USE_SENDMAIL
/*-------------------------------------------------------------------------------------------*/

void CmndSendmail(void) {
  if (XdrvMailbox.data_len > 0) {
    uint8_t result = SendMail(XdrvMailbox.data);
    char stemp1[20];
    ResponseCmndChar(GetTextIndexed(stemp1, sizeof(stemp1), result, kWebCmndStatus));
  }
}
#endif  // USE_SENDMAIL

/*-------------------------------------------------------------------------------------------*/

void CmndWebServer(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 2)) {
    Settings->webserver = XdrvMailbox.payload;
  }
  if (Settings->webserver) {
    Response_P(PSTR("{\"" D_CMND_WEBSERVER "\":\"" D_JSON_ACTIVE_FOR " %s " D_JSON_ON_DEVICE " %s " D_JSON_WITH_IP_ADDRESS " %_I\"}"),
      (2 == Settings->webserver) ? PSTR(D_ADMIN) : PSTR(D_USER), NetworkHostname(), (uint32_t)NetworkAddress());
  } else {
    ResponseCmndStateText(0);
  }
}

/*-------------------------------------------------------------------------------------------*/

void CmndWebPassword(void) {
  bool show_asterisk = (2 == XdrvMailbox.index);
  if (XdrvMailbox.data_len > 0) {
    SettingsUpdateText(SET_WEBPWD, (SC_CLEAR == Shortcut()) ? "" : (SC_DEFAULT == Shortcut()) ? WEB_PASSWORD : XdrvMailbox.data);
    if (!show_asterisk) {
      ResponseCmndChar(SettingsText(SET_WEBPWD));
    }
  } else {
    show_asterisk = true;
  }
  if (show_asterisk) {
    Response_P(S_JSON_COMMAND_ASTERISK, XdrvMailbox.command);
  }
}

/*-------------------------------------------------------------------------------------------*/

void CmndWeblog(void) {
  if ((XdrvMailbox.payload >= LOG_LEVEL_NONE) && (XdrvMailbox.payload <= LOG_LEVEL_DEBUG_MORE)) {
    Settings->weblog_level = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->weblog_level);
}

/*-------------------------------------------------------------------------------------------*/

void CmndWebRefresh(void) {
  if ((XdrvMailbox.payload > 399) && (XdrvMailbox.payload <= 65000)) {
    Settings->web_refresh = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->web_refresh);
}

/*-------------------------------------------------------------------------------------------*/

int WebSend(char *buffer) {
  // [tasmota] POWER1 ON                                               --> Sends http://tasmota/cm?cmnd=POWER1 ON
  // [192.168.178.86:80,admin:joker] POWER1 ON                        --> Sends http://hostname:80/cm?user=admin&password=joker&cmnd=POWER1 ON
  // [tasmota] /any/link/starting/with/a/slash.php?log=123             --> Sends http://tasmota/any/link/starting/with/a/slash.php?log=123
  // [tasmota,admin:joker] /any/link/starting/with/a/slash.php?log=123 --> Sends http://tasmota/any/link/starting/with/a/slash.php?log=123

  char *host;
  char *user;
  char *password;
  char *command;
  int status = WEBCMND_WRONG_PARAMETERS;

                                              // buffer = |  [  192.168.178.86  :  80  ,  admin  :  joker  ]    POWER1 ON   |
  host = strtok_r(buffer, "]", &command);     // host = |  [  192.168.178.86  :  80  ,  admin  :  joker  |, command = |    POWER1 ON   |
  if (host && command) {
    RemoveSpace(host);                        // host = |[192.168.178.86:80,admin:joker|
    host++;                                   // host = |192.168.178.86:80,admin:joker| - Skip [
    host = strtok_r(host, ",", &user);        // host = |192.168.178.86:80|, user = |admin:joker|
    String url = F("http://");                // url = |http://|
    url += host;                              // url = |http://192.168.178.86:80|

    command = Trim(command);                  // command = |POWER1 ON| or |/any/link/starting/with/a/slash.php?log=123|
    if (command[0] != '/') {
      url += F("/cm?");                       // url = |http://192.168.178.86/cm?|
      if (user) {
        user = strtok_r(user, ":", &password);  // user = |admin|, password = |joker|
        if (user && password) {
          char userpass[200];
          snprintf_P(userpass, sizeof(userpass), PSTR("user=%s&password=%s&"), user, password);
          url += userpass;                    // url = |http://192.168.178.86/cm?user=admin&password=joker&|
        }
      }
      url += F("cmnd=");                      // url = |http://192.168.178.86/cm?cmnd=| or |http://192.168.178.86/cm?user=admin&password=joker&cmnd=|
    }
    url += UrlEncode(command);                // url = |http://192.168.178.86/cm?cmnd=POWER1%20ON|
    url += F(" GET");                         // url = |http://192.168.178.86/cm?cmnd=POWER1%20ON GET|

    DEBUG_CORE_LOG(PSTR("WEB: Uri '%s'"), url.c_str());
    status = WebQuery(const_cast<char*>(url.c_str()));
  }
  return status;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void CmndWebSend(void) {
  if (XdrvMailbox.data_len > 0) {
    uint32_t result = WebSend(XdrvMailbox.data);
    if (result != WEBCMND_VALID_RESPONSE) {
      char stemp1[20];
      ResponseCmndChar(GetTextIndexed(stemp1, sizeof(stemp1), result, kWebCmndStatus));
    }
  }
}

/*-------------------------------------------------------------------------------------------*/

void CmndWebQuery(void) {
  if (XdrvMailbox.data_len > 0) {
    uint32_t result = WebQuery(XdrvMailbox.data);
    if (result != WEBCMND_VALID_RESPONSE) {
      char stemp1[20];
      ResponseCmndChar(GetTextIndexed(stemp1, sizeof(stemp1), result, kWebCmndStatus));
    }
  }
}

#ifdef USE_WEBRUN
/*-------------------------------------------------------------------------------------------*/

void CmndWebRun(void) {
  if (XdrvMailbox.data_len > 0) {
    uint32_t result = WebQuery(XdrvMailbox.data, QUERY_RUN);
    if (result != WEBCMND_VALID_RESPONSE) {
      char stemp1[20];
      ResponseCmndChar(GetTextIndexed(stemp1, sizeof(stemp1), result, kWebCmndStatus));
    }
  }
}
#endif // #ifdef USE_WEBRUN

#ifdef USE_WEBGETCONFIG
/*-------------------------------------------------------------------------------------------*/

int WebGetConfig(char *buffer) {
  // http://user:password@server:port/path/%id%.dmp  : %id% will be expanded to MAC address

  int status = WEBCMND_WRONG_PARAMETERS;

  RemoveSpace(buffer);                        // host = |[192.168.178.86:80,admin:joker|
  String url = ResolveToken(buffer);

  DEBUG_CORE_LOG(PSTR("WEB: Config Uri '%s'"), url.c_str());


#if defined(USE_WEBCLIENT_HTTPS)
  HTTPClientLight http;
  if (http.begin(UrlEncode(url))) {         // UrlEncode(url) = |http://192.168.178.86/cm?cmnd=POWER1%20ON|
#else // HTTP only
  WiFiClient http_client;
  HTTPClient http;
  if (http.begin(http_client, UrlEncode(url))) {  // UrlEncode(url) = |http://192.168.178.86/cm?cmnd=POWER1%20ON|
#endif
    int http_code = http.GET();             // Start connection and send HTTP header
    if (http_code > 0) {                    // http_code will be negative on error
      status = WEBCMND_DONE;
      if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_MOVED_PERMANENTLY) {
        WiFiClient *stream = http.getStreamPtr();
        int len = http.getSize();
        if (len <= sizeof(TSettings)) { 
          len = sizeof(TSettings);
        }
        if (SettingsBufferAlloc(len)) {
          uint8_t *buff = settings_buffer;
          while (http.connected() && (len > 0)) {
            size_t size = stream->available();
            if (size) {
              int read = stream->readBytes(buff, len);
              len -= read;
            }
            delayMicroseconds(1);
          }
          if (len) {
            DEBUG_CORE_LOG(PSTR("WEB: Connection lost"));
            status = WEBCMND_CONNECTION_LOST;
          } else if (SettingsConfigRestore()) {
            AddLog(LOG_LEVEL_INFO, PSTR("WEB: Settings applied, restarting"));
            TasmotaGlobal.restart_flag = 2;  // Always restart to re-enable disabled features during update
          } else {
            DEBUG_CORE_LOG(PSTR("WEB: Settings file invalid"));
            status = WEBCMND_INVALID_FILE;
          }
        } else {
          DEBUG_CORE_LOG(PSTR("WEB: Memory error (%d) or invalid file length (%d)"), settings_buffer, len);
          status = WEBCMND_MEMORY_ERROR;
        }
      } else {
        AddLog(LOG_LEVEL_DEBUG, PSTR("WEB: HTTP error %d"), http_code);
        status = (http_code == HTTP_CODE_NOT_FOUND) ? WEBCMND_FILE_NOT_FOUND : WEBCMND_OTHER_HTTP_ERROR;
      }
    } else {
      DEBUG_CORE_LOG(PSTR("WEB: Connection failed"));
      status = 2;                           // Connection failed
    }
    http.end();                             // Clean up connection data
  } else {
    status = 3;                             // Host not found or connection error
  }

  return status;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void CmndWebGetConfig(void) {
  // WebGetConfig http://myserver:8000/tasmota/conf/%id%.dmp where %id% is expanded to device mac address
  // WebGetConfig http://myserver:8000/tasmota/conf/Config_demo_9.5.0.8.dmp
  if (XdrvMailbox.data_len > 0) {
    uint32_t result = WebGetConfig(XdrvMailbox.data);
    char stemp1[20];
    ResponseCmndChar(GetTextIndexed(stemp1, sizeof(stemp1), result, kWebCmndStatus));
  }
}
#endif // USE_WEBGETCONFIG

/*-------------------------------------------------------------------------------------------*/

bool JsonWebColor(const char* dataBuf) {
  // Default (Dark theme)
  // {"WebColor":["#eaeaea","#252525","#4f4f4f","#000","#ddd","#65c115","#1f1f1f","#ff5661","#008000","#faffff","#1fa3ec","#0e70a4","#d43535","#931f1f","#47c266","#5aaf6f","#faffff","#999","#eaeaea","#08405e"]}
  // Default pre v7 (Light theme)
  // {"WebColor":["#000","#fff","#f2f2f2","#000","#fff","#000","#fff","#f00","#008000","#fff","#1fa3ec","#0e70a4","#d43535","#931f1f","#47c266","#5aaf6f","#fff","#999","#000","#08405e"]}
  // {"WebColor":["#000000","#ffffff","#f2f2f2","#000000","#ffffff","#000000","#ffffff","#ff0000","#008000","#ffffff","#1fa3ec","#0e70a4","#d43535","#931f1f","#47c266","#5aaf6f","#ffffff","#999999","#000000","#08405e"]}

  JsonParser parser((char*) dataBuf);
  JsonParserObject root = parser.getRootObject();
  JsonParserArray arr = root[PSTR(D_CMND_WEBCOLOR)].getArray();
  if (arr) {  // if arr is valid, i.e. json is valid, the key D_CMND_WEBCOLOR was found and the token is an arra
    uint32_t i = 0;
    for (auto color : arr) {
      if (i < COL_LAST) {
        WebHexCode(i, color.getStr());
      } else {
        break;
      }
      i++;
    }
  }
  return true;
}

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void CmndWebColor(void) {
  if (XdrvMailbox.data_len > 0) {
    if (strchr(XdrvMailbox.data, '{') == nullptr) {  // If no JSON it must be parameter
      if ((XdrvMailbox.data_len > 3) && (XdrvMailbox.index > 0) && (XdrvMailbox.index <= COL_LAST)) {
        WebHexCode(XdrvMailbox.index -1, XdrvMailbox.data);
      }
      else if (0 == XdrvMailbox.payload) {
        SettingsDefaultWebColor();
      }
    }
    else {
      JsonWebColor(XdrvMailbox.data);
    }
  }
  Response_P(PSTR("{\"%s\":["), XdrvMailbox.command);
  for (uint32_t i = 0; i < COL_LAST; i++) {
    ResponseAppend_P(PSTR("%s\"#%06x\""), (i>0)?",":"", WebColor(i));
  }
  ResponseAppend_P(PSTR("]}"));
}

/*-------------------------------------------------------------------------------------------*/

void CmndWebSensor(void) {
  if (XdrvMailbox.index < MAX_XSNS_DRIVERS) {
    if (XdrvMailbox.payload >= 0) {
      bitWrite(Settings->sensors[1][XdrvMailbox.index / 32], XdrvMailbox.index % 32, XdrvMailbox.payload &1);
    }
  }
  Response_P(PSTR("{\"" D_CMND_WEBSENSOR "\":"));
  XsnsSensorState(1);
  ResponseJsonEnd();
}

/*-------------------------------------------------------------------------------------------*/

String *WebButton1732[16] = {0,};

void SetWebButton(uint8_t button_index, const char *text) {
  if (button_index < 16) 
    SettingsUpdateText(SET_BUTTON1 + button_index, text);
  else if (button_index < MAX_BUTTON_TEXT) {
    button_index -= 16;
    if (!WebButton1732[button_index]) 
      WebButton1732[button_index] = new String(text);
    else
      *WebButton1732[button_index] = text;
  }
}

const char* GetWebButton(uint8_t button_index) {
  static char empty[1] = {0};
  if (button_index < 16) 
    return SettingsText(SET_BUTTON1 + button_index);
  else if (button_index < MAX_BUTTON_TEXT) {
    button_index -= 16;
    if (WebButton1732[button_index])
      return WebButton1732[button_index]->c_str();
  }
  return empty;
}

void CmndWebButton(void) {
  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= MAX_BUTTON_TEXT)) {
    if (!XdrvMailbox.usridx) {
      ResponseCmndAll(SET_BUTTON1, MAX_BUTTON_TEXT);
    } else {
      if (XdrvMailbox.data_len > 0) {
        SetWebButton(XdrvMailbox.index -1, ('"' == XdrvMailbox.data[0]) ? "" : XdrvMailbox.data);
      }
      ResponseCmndIdxChar(GetWebButton(XdrvMailbox.index -1));
    }
  }
}

/*-------------------------------------------------------------------------------------------*/

void CmndWebCanvas(void) {
  /*
  WebCanvas allows GUI body canvas configuration using a color, "url" or "gradient".
  The provided text overrules the body CSS background property "body{background:<WebCanvas> 0 0 / cover no-repeat fixed;}"
  - WebCanvas "                                                            // Set canvas to WebColor2
  - WebCanvas 0                                                            // Set canvas to WebColor2
  - WebCanvas red                                                          // Red canvas
  - WebCanvas linear-gradient(#F02 7%,#F93,#FF4,#082,#00F,#708 93%)        // Gradient pride flag
  - WebCanvas linear-gradient(#F02 16%,#F93 16% 33%,#FF4 33% 50%,#082 50% 67%,#00F 67% 84%,#708 84%)  // Pride flag
  - WebCanvas linear-gradient(90deg,#05A 33%,#FFF 33% 67%,#F43 67%)        // Flag France
  - WebCanvas linear-gradient(#000 33%,#D00 33% 67%,#FC0 67%)              // Flag Germany
  - WebCanvas linear-gradient(#059 33%,#FFF 33% 67%,#F43 67%)              // Flag The Netherlands
  - WebCanvas linear-gradient(#05B 50%,#FD0 50%)                           // Flag Ukraine
  - WebCanvas linear-gradient(#FFF 33%,#07D 33% 67%,#F34 67%)              // Flag Russia
  - WebCanvas url(http://ota.tasmota.com/tasmota/images/prf.png)           // Pride flag
  - WebCanvas url(http://ota.tasmota.com/tasmota/images/tasmota_logo.png)  // Tasmota logo
  */
  if (XdrvMailbox.data_len > 0) {
    SettingsUpdateText(SET_CANVAS, (SC_CLEAR == Shortcut()) ? "" : XdrvMailbox.data);
  }
  ResponseCmndChar(SettingsText(SET_CANVAS));
}

#ifdef USE_CORS
/*-------------------------------------------------------------------------------------------*/

void CmndCors(void) {
  if (XdrvMailbox.data_len > 0) {
    SettingsUpdateText(SET_CORS, (SC_CLEAR == Shortcut()) ? "" : (SC_DEFAULT == Shortcut()) ? CORS_DOMAIN : XdrvMailbox.data);
  }
  ResponseCmndChar(SettingsText(SET_CORS));
}
#endif  // USE_CORS

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv01(uint32_t function) {
  bool result = false;

  switch (function) {
    case FUNC_LOOP:
      PollDnsWebserver();
#ifdef USE_WEBRUN
      WebRunLoop();
#endif // #ifdef USE_WEBRUN
      break;
    case FUNC_EVERY_SECOND:
      if (Web.initial_config) {
        Wifi.config_counter = 200;    // Do not restart the device if it has SSId Blank
      }
      if (Wifi.wifi_test_counter) {
        Wifi.wifi_test_counter--;
        AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_WIFI D_TRYING_TO_CONNECT " %s"), SettingsText(SET_STASSID1));
        IPAddress local_ip;
        if (WifiGetIP(&local_ip, true)) {            // Got IP - Connection Established (exclude AP address)
          Wifi.wifi_test_AP_TIMEOUT = false;
          Wifi.wifi_test_counter = 0;
          Wifi.wifiTest = WIFI_TEST_FINISHED;
          AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CMND_SSID " %s: " D_CONNECTED " - " D_IP_ADDRESS " %s"), SettingsText(Wifi.wifi_Test_Save_SSID2 ? SET_STASSID2 : SET_STASSID1), local_ip.toString().c_str());
//          TasmotaGlobal.blinks = 255;                    // Signal wifi connection with blinks
          if (MAX_WIFI_OPTION != Wifi.old_wificonfig) {
            TasmotaGlobal.wifi_state_flag = Settings->sta_config = Wifi.old_wificonfig;
          }
          TasmotaGlobal.save_data_counter = Wifi.save_data_counter;
          Settings->save_data = Wifi.save_data_counter;
          SettingsSaveAll();

          if ( Wifi.wifi_Test_Restart ) { TasmotaGlobal.restart_flag = 2; }

#if (!RESTART_AFTER_INITIAL_WIFI_CONFIG)
          Web.initial_config = false;
          Web.state = HTTP_ADMIN;
#endif
        } else if (!Wifi.wifi_test_counter) { // Test TimeOut
          Wifi.wifi_test_counter = 0;
          Wifi.wifiTest = WIFI_TEST_FINISHED_BAD;
          switch (WiFi.status()) {
            case WL_CONNECTED:
              AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CONNECT_FAILED_NO_IP_ADDRESS));
              Wifi.wifi_test_AP_TIMEOUT = false;
              break;
            case WL_NO_SSID_AVAIL:
              AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CONNECT_FAILED_AP_NOT_REACHED));
              Wifi.wifi_test_AP_TIMEOUT = false;
              break;
            case WL_CONNECT_FAILED:
              AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CONNECT_FAILED_WRONG_PASSWORD));
              Wifi.wifi_test_AP_TIMEOUT = false;
              break;
            default:  // WL_IDLE_STATUS and WL_DISCONNECTED - SSId in range but no answer from the router
              AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CONNECT_FAILED_AP_TIMEOUT));
              // If this error occurs twice, Tasmota will connect directly to the router without testing credentials.
              //   ESP8266 in AP+STA mode can manage only 11b and 11g, so routers that are 11n-ONLY won't respond.
              //   For this case, the user will see in the UI a message to check credentials. After that, if the user hits
              //   save and connect again, and the CONNECT_FAILED_AP_TIMEOUT is shown again, Credentials will be saved and
              //   Tasmota will restart and try to connect in STA mode only (11b/g/n).
              //
              //   If it fails again, depending on the WIFICONFIG settings, the user will need to wait or will need to
              //   push 6 times the button to enable Tasmota AP mode again.
              if (Wifi.wifi_test_AP_TIMEOUT) {
                Wifi.wifiTest = WIFI_TEST_FINISHED;
                AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_WIFI D_CMND_SSID " %s: " D_ATTEMPTING_CONNECTION), SettingsText(Wifi.wifi_Test_Save_SSID2 ? SET_STASSID2 : SET_STASSID1) );
                if (MAX_WIFI_OPTION != Wifi.old_wificonfig) {
                  TasmotaGlobal.wifi_state_flag = Settings->sta_config = Wifi.old_wificonfig;
                }
                TasmotaGlobal.save_data_counter = Wifi.save_data_counter;
                Settings->save_data = Wifi.save_data_counter;
                SettingsSaveAll();
              }
              Wifi.wifi_test_AP_TIMEOUT = true;
          }
          WiFi.scanNetworks(); // restart scan
        }
      }
      break;
    case FUNC_COMMAND:
      result = DecodeCommand(kWebCommands, WebCommand);
      break;
    case FUNC_ACTIVE:
      result = true;
      break;
  }
  return result;
}
#endif  // USE_WEBSERVER
