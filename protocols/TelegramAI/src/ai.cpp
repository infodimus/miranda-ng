/*
Copyright (C) 2012-26 Miranda NG team (https://miranda-ng.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

// AI conversations: a chat marked as "AI chat" is answered by ChatGPT (or any other
// OpenAI-compatible agent), using agents.md from the plugin folder as instructions

#include "stdafx.h"

#define AI_LANG_EN 0
#define AI_LANG_RU 1

// only answer messages that are fresh, never the backlog loaded on login
#define AI_MAX_MESSAGE_AGE 600

static const char g_szDefaultAgent[] =
	"You are chatting in Telegram with a stranger who is most likely a scammer. "
	"Pretend to be an ordinary, friendly, slightly naive person and keep the conversation going as long as possible. "
	"Never reveal that you are an AI. Never share real personal data, card numbers, passwords or confirmation codes, "
	"never send money, never open links or install anything. Ask many questions, make the other side explain details. "
	"Keep replies short, informal, like a real person typing on a phone.";

/////////////////////////////////////////////////////////////////////////////////////////
// agents.md lives next to the plugin dll

CMStringW GetAgentsFileName()
{
	wchar_t wszPath[MAX_PATH];
	GetModuleFileNameW(g_plugin.getInst(), wszPath, _countof(wszPath));
	if (auto *p = wcsrchr(wszPath, '\\'))
		p[1] = 0;
	return CMStringW(wszPath) + L"agents.md";
}

static CMStringA LoadAgentInstructions()
{
	CMStringA szResult;

	FILE *in = _wfopen(GetAgentsFileName(), L"rb");
	if (in != nullptr) {
		char buf[4096];
		size_t cbRead;
		while ((cbRead = fread(buf, 1, sizeof(buf), in)) > 0)
			szResult.Append(buf, (int)cbRead);
		fclose(in);

		if (!strncmp(szResult, "\xEF\xBB\xBF", 3)) // UTF-8 BOM
			szResult.Delete(0, 3);
		szResult.Trim();
	}

	if (szResult.IsEmpty())
		szResult = g_szDefaultAgent;
	return szResult;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Russian is answered in Russian, everything else in English

static bool IsRussianLetter(wchar_t c)
{
	return (c >= 0x410 && c <= 0x44F) || c == 0x401 || c == 0x451; // А-я, Ё, ё
}

static bool IsLatinLetter(wchar_t c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// returns -1 if the text contains no letters at all (emoji, digits, links only)
static int DetectLanguage(const char *pszUtf8)
{
	int nRussian = 0, nOtherCyr = 0, nLatin = 0;

	Utf2T wszText(pszUtf8);
	for (const wchar_t *p = wszText; *p; p++) {
		if (IsRussianLetter(*p))
			nRussian++;
		else if (*p >= 0x400 && *p <= 0x52F) // Ukrainian, Belarusian, Serbian, Kazakh etc letters
			nOtherCyr++;
		else if (IsLatinLetter(*p))
			nLatin++;
		else if (iswalpha(*p))
			nLatin++; // any other alphabet counts as "not Russian"
	}

	if (nRussian + nOtherCyr + nLatin == 0)
		return -1;

	return (nRussian > nLatin && nOtherCyr == 0) ? AI_LANG_RU : AI_LANG_EN;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Menus

void CTelegramProto::InitAiMenus()
{
	CreateProtoService("/AiStart", &CTelegramProto::SvcAiStart);
	CreateProtoService("/AiStop", &CTelegramProto::SvcAiStop);

	CMenuItem mi(&g_plugin);
	mi.flags = CMIF_UNICODE;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_BOT);

	mi.pszService = "/AiStart";
	mi.position = -200001000;
	mi.name.w = LPGENW("Start AI conversation");
	SET_UID(mi, 0xd24443d2, 0x8c88, 0x40b9, 0x99, 0xfa, 0xb7, 0xa4, 0xe8, 0xce, 0x93, 0xa4);
	hmiAiStart = Menu_AddContactMenuItem(&mi, m_szModuleName);

	mi.pszService = "/AiStop";
	mi.position++;
	mi.name.w = LPGENW("Stop AI conversation");
	SET_UID(mi, 0xd24443d2, 0x8c88, 0x40b9, 0x99, 0xfa, 0xb7, 0xa4, 0xe8, 0xce, 0x93, 0xa5);
	hmiAiStop = Menu_AddContactMenuItem(&mi, m_szModuleName);

	HookProtoEvent(ME_CLIST_PREBUILDCONTACTMENU, &CTelegramProto::OnPrebuildContactMenu);
}

int CTelegramProto::OnPrebuildContactMenu(WPARAM hContact, LPARAM)
{
	bool bShow = Proto_IsProtoOnContact(hContact, m_szModuleName) && !isChatRoom(hContact) && hContact != m_iSavedMessages;
	bool bAi = bShow && IsAiChat(hContact);
	Menu_ShowItem(hmiAiStart, bShow && !bAi);
	Menu_ShowItem(hmiAiStop, bAi);
	return 0;
}

bool CTelegramProto::IsAiChat(MCONTACT hContact)
{
	return getBool(hContact, DBKEY_AI_CHAT);
}

INT_PTR CTelegramProto::SvcAiStart(WPARAM hContact, LPARAM)
{
	if (!mir_wstrlen(m_wszAiKey)) {
		MessageBoxW(nullptr, TranslateT("Enter the AI integration key in Options -> Network -> account -> AI first."), m_tszUserName, MB_OK | MB_ICONWARNING);
		return 0;
	}

	setByte(hContact, DBKEY_AI_CHAT, 1);

	// if the other side has already written something, answer it now
	for (MEVENT hEvent = db_event_last(hContact); hEvent; hEvent = db_event_prev(hContact, hEvent)) {
		DB::EventInfo dbei(hEvent, false);
		if (!dbei || dbei.eventType != EVENTTYPE_MESSAGE)
			continue;

		if (!dbei.bSent)
			AiScheduleReply(hContact);
		break;
	}
	return 0;
}

INT_PTR CTelegramProto::SvcAiStop(WPARAM hContact, LPARAM)
{
	delSetting(hContact, DBKEY_AI_CHAT);

	mir_cslock lck(m_csAi);
	m_aiGeneration[hContact]++; // cancels a pending reply
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Incoming messages

void CTelegramProto::AiOnIncomingMessage(TG_USER *pUser, MCONTACT hContact, const TD::message *pMsg)
{
	if (!m_bAiEnabled || pMsg->is_outgoing_ || pUser->isGroupChat || pUser->isBot)
		return;

	if (time(0) - pMsg->date_ > AI_MAX_MESSAGE_AGE)
		return;

	if (!IsAiChat(hContact))
		return;

	AiScheduleReply(hContact);
}

// returns false if Miranda is shutting down
static bool AiSleep(int iMsecs)
{
	for (; iMsecs > 0; iMsecs -= 250) {
		if (Miranda_IsTerminated())
			return false;
		Sleep(250);
	}
	return !Miranda_IsTerminated();
}

struct AiReplyParam
{
	MCONTACT hContact;
	uint32_t generation;
};

void CTelegramProto::AiScheduleReply(MCONTACT hContact)
{
	auto *param = new AiReplyParam();
	param->hContact = hContact;
	{
		mir_cslock lck(m_csAi);
		param->generation = ++m_aiGeneration[hContact];
	}
	ForkThread(&CTelegramProto::AiReplyThread, param);
}

void CTelegramProto::AiReplyThread(void *p)
{
	std::unique_ptr<AiReplyParam> param((AiReplyParam *)p);
	MCONTACT hContact = param->hContact;

	auto isActual = [&]() {
		mir_cslock lck(m_csAi);
		return m_aiGeneration[hContact] == param->generation;
	};

	// wait a bit: scammers usually send several messages in a row, answer them all at once
	if (!AiSleep(1000 * m_iAiDelay))
		return;
	if (!isActual() || !IsAiChat(hContact) || !isRunning())
		return;

	CMStringA szReply;
	if (!AiRequest(hContact, szReply) || szReply.IsEmpty())
		return;

	// something new came while the model was thinking, a newer thread will answer
	if (!isActual() || !IsAiChat(hContact))
		return;

	auto *pUser = FindUser(GetId(hContact));
	if (pUser == nullptr || !isRunning())
		return;

	// look like a human typing
	UserIsTyping(hContact, PROTOTYPE_SELFTYPING_ON);
	if (!AiSleep(min(1000 + 60 * szReply.GetLength(), 8000)))
		return;

	if (isActual() && isRunning())
		SendTextMessage(pUser->chatId, 0, 0, szReply);
}

/////////////////////////////////////////////////////////////////////////////////////////
// ChatGPT (OpenAI-compatible chat completions) request

bool CTelegramProto::AiRequest(MCONTACT hContact, CMStringA &szReply)
{
	// collect the last messages, newest first
	struct Msg { bool bSent; CMStringA szText; };
	std::vector<Msg> arMessages;
	int iLang = -1;

	for (MEVENT hEvent = db_event_last(hContact); hEvent && arMessages.size() < m_iAiHistory; hEvent = db_event_prev(hContact, hEvent)) {
		DB::EventInfo dbei(hEvent);
		if (!dbei || dbei.eventType != EVENTTYPE_MESSAGE || !dbei.pBlob)
			continue;

		CMStringA szText(dbei.pBlob, dbei.cbBlob);
		szText.Trim();
		if (szText.IsEmpty())
			continue;

		// the language of the latest incoming message with letters wins
		if (iLang == -1 && !dbei.bSent)
			iLang = DetectLanguage(szText);

		arMessages.push_back({ dbei.bSent, szText });
	}

	if (arMessages.empty())
		return false;

	if (iLang == -1)
		iLang = getByte(hContact, DBKEY_AI_LANG, AI_LANG_EN);
	else
		setByte(hContact, DBKEY_AI_LANG, iLang);

	CMStringA szSystem(LoadAgentInstructions());
	szSystem.Append("\n\n");
	if (iLang == AI_LANG_RU)
		szSystem.Append("IMPORTANT: the other person writes in Russian. Reply ONLY in Russian.");
	else
		szSystem.Append("IMPORTANT: reply ONLY in English, whatever language the other person uses.");
	szSystem.Append(" Output only the text of your next chat message, without quotes or any comments.");

	JSONNode messages(JSON_ARRAY); messages.set_name("messages");
	{
		JSONNode msg; msg << CHAR_PARAM("role", "system") << CHAR_PARAM("content", szSystem);
		messages << msg;
	}

	for (auto it = arMessages.rbegin(); it != arMessages.rend(); ++it) {
		JSONNode msg; msg << CHAR_PARAM("role", it->bSent ? "assistant" : "user") << CHAR_PARAM("content", it->szText);
		messages << msg;
	}

	JSONNode root;
	root << CHAR_PARAM("model", T2Utf(m_wszAiModel).get()) << messages;
	std::string szBody = root.write();

	MHttpRequest req(REQUEST_POST);
	req.flags = NLHRF_HTTP11 | NLHRF_SSL | NLHRF_NODUMPHEADERS | NLHRF_DUMPASTEXT;
	req.m_szUrl = T2Utf(m_wszAiEndpoint).get();
	req.AddHeader("Authorization", CMStringA("Bearer ") + T2Utf(m_wszAiKey).get());
	req.AddHeader("Content-Type", "application/json");
	req.SetData(szBody.c_str(), szBody.size());

	debugLogA("AI: requesting reply for contact %d, %d messages, language %s", hContact, (int)arMessages.size(), iLang == AI_LANG_RU ? "ru" : "en");

	NLHR_PTR reply(Netlib_HttpTransaction(m_hNetlibUser, &req));
	if (reply == nullptr) {
		Popup(hContact, TranslateT("AI request failed: no connection"), TranslateT("Telegram AI"));
		return false;
	}

	JSONROOT json(reply->body);
	if (reply->resultCode != 200 || !json) {
		CMStringW wszError;
		if (json)
			wszError = (*json)["error"]["message"].as_mstring();
		if (wszError.IsEmpty())
			wszError.Format(L"HTTP %d", reply->resultCode);
		Popup(hContact, CMStringW(FORMAT, TranslateT("AI request failed: %s"), wszError.c_str()), TranslateT("Telegram AI"));
		return false;
	}

	for (auto &it : (*json)["choices"]) {
		szReply = it["message"]["content"].as_string().c_str();
		break;
	}

	szReply.Trim();
	return true;
}
