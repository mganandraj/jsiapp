#include "stdafx.h"
#include "scripthost.h"

int APIENTRY winHostSetup(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR    lpCmdLine, int       nCmdShow);

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
  ScriptHost scriptHost;

  std::string script("mylogger('hello v8...');"
    "//mylogger('hello v8 ... its me ...');"
    "//throw 10;");
  scriptHost.runScript(script);

  winHostSetup(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}