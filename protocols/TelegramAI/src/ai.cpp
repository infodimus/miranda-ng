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

	// partial history loads for very long chats
	CreateProtoService("/LoadHistory100", &CTelegramProto::SvcLoadHistory100);
	CreateProtoService("/LoadHistory10Days", &CTelegramProto::SvcLoadHistory10Days);

	mi.hIcolibItem = Skin_GetIconHandle(SKINICON_OTHER_HISTORY);

	mi.pszService = "/LoadHistory100";
	mi.position = -200000900;
	mi.name.w = LPGENW("Load 100 older messages");
	SET_UID(mi, 0xd24443d2, 0x8c88, 0x40b9, 0x99, 0xfa, 0xb7, 0xa4, 0xe8, 0xce, 0x93, 0xa6);
	hmiHistory100 = Menu_AddContactMenuItem(&mi, m_szModuleName);

	mi.pszService = "/LoadHistory10Days";
	mi.position++;
	mi.name.w = LPGENW("Load 10 more days of history");
	SET_UID(mi, 0xd24443d2, 0x8c88, 0x40b9, 0x99, 0xfa, 0xb7, 0xa4, 0xe8, 0xce, 0x93, 0xa7);
	hmiHistory10Days = Menu_AddContactMenuItem(&mi, m_szModuleName);

	HookProtoEvent(ME_CLIST_PREBUILDCONTACTMENU, &CTelegramProto::OnPrebuildContactMenu);
}

int CTelegramProto::OnPrebuildContactMenu(WPARAM hContact, LPARAM)
{
	bool bShow = false;
	if (Proto_IsProtoOnContact(hContact, m_szModuleName) && hContact != m_iSavedMessages) {
		// channels can't be answered, a forum is answered in its topics, not in the root
		TD::int53 threadId;
		if (auto *pUser = AiFindChat(hContact, threadId))
			bShow = !pUser->isChannel && !(pUser->isForum && !threadId);
	}
	bool bAi = bShow && IsAiChat(hContact);
	Menu_ShowItem(hmiAiStart, bShow && !bAi);
	Menu_ShowItem(hmiAiStop, bAi);

	bool bHistory = Proto_IsProtoOnContact(hContact, m_szModuleName);
	Menu_ShowItem(hmiHistory100, bHistory);
	Menu_ShowItem(hmiHistory10Days, bHistory);
	return 0;
}

// private chats, groups and forum topics (their contacts have no own id, only owner + thread)

TG_USER* CTelegramProto::AiFindChat(MCONTACT hContact, TD::int53 &threadId)
{
	threadId = GetId(hContact, DBKEY_THREAD);
	return FindUser(threadId ? GetId(hContact, DBKEY_OWNER) : GetId(hContact));
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
	if (!m_bAiEnabled || pMsg->is_outgoing_ || pUser->isBot || pUser->isChannel)
		return;

	// never talk to bots in groups, two robots would chat forever
	if (pUser->isGroupChat)
		if (auto *pSender = GetSender(pMsg->sender_id_.get()))
			if (pSender->isBot || pSender->id == m_iOwnId)
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

	TD::int53 threadId;
	auto *pUser = AiFindChat(hContact, threadId);
	if (pUser == nullptr || pUser->chatId == -1)
		return;

	CMStringA szReply;
	TD::int53 replyId = 0;
	if (!AiRequest(hContact, pUser->isGroupChat, szReply, replyId) || szReply.IsEmpty())
		return;

	// something new came while the model was thinking, a newer thread will answer
	if (!isActual() || !IsAiChat(hContact) || !isRunning())
		return;

	if (Contact::IsReadonly(hContact)) {
		debugLogA("AI: chat %lld is read only, reply dropped", pUser->chatId);
		return;
	}

	// look like a human typing
	SendQuery(new TD::sendChatAction(pUser->chatId, 0, "", TD::make_object<TD::chatActionTyping>()));
	if (!AiSleep(min(1000 + 60 * szReply.GetLength(), 8000)))
		return;

	// in a group the reply is attached to the message it answers, so it's clear whom we talk to
	if (isActual() && isRunning())
		SendTextMessage(pUser->chatId, threadId, replyId, szReply);
}

/////////////////////////////////////////////////////////////////////////////////////////
// ChatGPT (OpenAI-compatible chat completions) request

bool CTelegramProto::AiRequest(MCONTACT hContact, bool bGroup, CMStringA &szReply, TD::int53 &replyId)
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

		// several people talk in a group: the model has to know who said what
		if (bGroup && !dbei.bSent) {
			if (replyId == 0)
				replyId = dbei2id(dbei);
			if (mir_strlen(dbei.szUserId))
				szText.Insert(0, CMStringA(dbei.szUserId) + ": ");
		}

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
	if (bGroup)
		szSystem.Append("\n\nThis is a group chat with several participants. Each of their messages starts with the sender's name and a colon. "
			"Answer as one more participant, address people by name when needed, and never start your reply with your own name.");

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

	debugLogA("AI: requesting reply for %s %d, %d messages, language %s", bGroup ? "group" : "contact", hContact, (int)arMessages.size(), iLang == AI_LANG_RU ? "ru" : "en");

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
