#include "Thing.h"

C3DVector* (__thiscall* CThing::OGetPos)(CThing*) = nullptr;
C3DVector* __fastcall CThing::HGetPos(CThing* _this, void* _EDX) {
	return OGetPos(_this);
}

C3DVector* CThing::GetPos() {
	return OGetPos(this);
}

int (__thiscall* CThing::OGetJoystickDeviceNumber)(CThing*) = nullptr;
int __fastcall CThing::HGetJoystickDeviceNumber(CThing* _this, void* _EDX) {
	//return OGetJoystickDeviceNumber(_this); // CRASH: if joystick is not 0 (AddRumble)
	return 0;
}

CDefString* (__thiscall* CThing::OGetDefName)(CThing*, CDefString*) = nullptr;
CDefString* __fastcall CThing::HGetDefName(CThing* _this, void* _EDX, CDefString* result) {
	return OGetDefName(_this, result);
}

CDefString* CThing::GetDefName(CDefString* result) {
	return OGetDefName(this, result);
}

void CThing::Hook()
{
	ADD_HOOK(0x004C73D0, HGetPos, OGetPos);
	ADD_HOOK(0x004C7CA0, HGetJoystickDeviceNumber, OGetJoystickDeviceNumber);
	ADD_HOOK(0x004C7CC0, HGetDefName, OGetDefName);
}
