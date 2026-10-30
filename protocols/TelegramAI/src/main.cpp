#include "stdafx.h"

int hLangpack;
CMPlugin g_plugin;

#pragma comment(lib, "tdapi.lib")

/////////////////////////////////////////////////////////////////////////////////////////

PLUGININFOEX pluginInfo =
{
	sizeof(PLUGININFOEX),
	__PLUGIN_NAME,
	PLUGIN_MAKE_VERSION(__MAJOR_VERSION, __MINOR_VERSION, __RELEASE_NUM, __BUILD_NUM),
	__DESCRIPTION,
	__AUTHOR,
	__COPYRIGHT,
	__AUTHORWEB,
	UNICODE_AWARE,
	// {7004256B-610D-45E3-839D-C121F8A59ADF}
	{0x7004256b, 0x610d, 0x45e3, {0x83, 0x9d, 0xc1, 0x21, 0xf8, 0xa5, 0x9a, 0xdf}}
};

CMPlugin::CMPlugin() :
	ACCPROTOPLUGIN<CTelegramProto>("TelegramAI", pluginInfo)
{
	SetUniqueId(DBKEY_ID);
}

/////////////////////////////////////////////////////////////////////////////////////////

extern "C" __declspec(dllexport) const MUUID MirandaInterfaces[] = { MIID_PROTOCOL, MIID_LAST };

/////////////////////////////////////////////////////////////////////////////////////////

static IconItem iconList[] =
{
	{ LPGEN("Telegram Premium user"), "premuim", IDI_PREMIUM },
	{ LPGEN("Reaction"), "reaction", IDI_REACTION },
	{ LPGEN("Bot"), "bot", IDI_BOT },
};

static int OnModuleLoaded(WPARAM, LPARAM)
{
	g_plugin.hasNewStory = ServiceExists("NewStory/RemoteRead");
	g_plugin.hasMessageState = ServiceExists(MS_MESSAGESTATE_UPDATE);
	return 0;
}

static int OnModulesLoaded(WPARAM, LPARAM)
{
	HookEvent(ME_SYSTEM_MODULELOAD, OnModuleLoaded);
	HookEvent(ME_SYSTEM_MODULEUNLOAD, OnModuleLoaded);
	OnModuleLoaded(0, 0);
	return 0;
}

int CMPlugin::Load()
{
	registerIcon("Protocols/Telegram AI", iconList, "tgai");

	HookEvent(ME_SYSTEM_MODULESLOADED, OnModulesLoaded);

	m_hIcon = ExtraIcon_RegisterIcolib("tgai_premium", LPGEN("Telegram AI Premium user"), getIconHandle(IDI_PREMIUM));
	return 0;
}
