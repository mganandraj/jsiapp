#include "stdafx.h"
#include "scripthost.h"

#include <iostream>

int APIENTRY winHostSetup(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR    lpCmdLine, int       nCmdShow, EventLoop&);




//int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
int main()
{
	
	ScriptHost scriptHost;
	std::string script("function getData() { print('abcd');} setImmediate(getData);");

  //std::string script("function getData() { return 100;}",
	//"//setImmediate(function(){ mylogger('Hello'); });"
 //   "//mylogger('main');"
 //   "//mylogger('hello v8...');"
 //   "//mylogger('hello v8 ... its me ...');"
 //   "//throw 10;");

  scriptHost.runScript(script);

  scriptHost.eventLoop_.loop();

  scriptHost.eventLoop_.t_.join();

  // winHostSetup(hInstance, hPrevInstance, lpCmdLine, nCmdShow, scriptHost.eventLoop_);
}