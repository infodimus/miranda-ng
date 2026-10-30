**English** | [Русский](README.ru.md)

# How to create a Telegram test account

A guide to Telegram's test servers for testing your own Telegram clients and plugins. Checked October 2026.

> [!IMPORTANT]
> **The old method no longer works.**
> The old `99966XYYYY` test numbers with code `XXXXX` (for example `+9996621234` / `22222`) are no longer accepted. The test servers reject the code on every data center. Test accounts now use a **real phone number**, and the first login code arrives in that number's **real** Telegram account.

## What you need

- A real mobile number, for example **+44-7777-1234567**.
- A real (production) Telegram account on that number, logged in on any client: phone, desktop or web.
- An Android phone with **Telegram Beta** (the official beta build). It lets you pick the test server on its login screen.

## Create the test account

The example number is +44-7777-1234567. Use your own.

1. Log into your **real** Telegram account with +44-7777-1234567, if you aren't logged in already. Any client works.
2. Install **Telegram Beta** on an Android phone.
3. In Telegram Beta, on the login screen, choose **Test Server**.
4. Enter the same real number: **+44-7777-1234567**.
5. The login code is **not** sent by SMS. It arrives as a message from "Telegram" in your **real** account from step 1.
6. Enter that code in Telegram Beta.
7. If the number is new on the test server, Telegram asks for a first and last name. Enter any name to finish sign-up.

> [!TIP]
> **Done.** You are now logged into your **test account** on +44-7777-1234567. The test account and the real account share the phone number but are **not related**: contacts, chats, name and settings are completely separate.

## Log into the test account on another device or client

This only works with clients that support the test server.

1. In that client, choose the test server, then enter **+44-7777-1234567**.
2. The code will most likely arrive in the test account that is already logged in (e.g. Telegram Beta from the steps above), as a message from "Telegram".
3. If it doesn't arrive there, check your real account. Telegram may still send it there, especially when no device is logged into the test account any more.

## Miranda NG with the TelegramAI plugin

The plugin can't sign up a new number. Create the test account in Telegram Beta first, as described above.

1. Open the TelegramAI account's options. If it's logged in, click **Log out**.
2. Country: **United Kingdom**. Phone: **77771234567** (the number without the country code).
3. Tick **Use test server** and click **Apply**.
4. Go online and enter the code. It will most likely arrive in Telegram Beta.

## Testing chats

- You can't really chat with yourself, so you need **two** test accounts. Repeat the steps with a second real number, such as a spare SIM or a family member's number.
- Test accounts can only find and message other test accounts. Real users, bots and channels are not visible there.

## Things to know

- Telegram **periodically wipes** the test servers. Don't keep anything important there.
- Flood limits apply on the test servers too. Don't request codes too often.
- **Keep Telegram Beta logged into the test account.** That is where later login codes arrive.
- Other clients: Telegram Desktop has a test mode (Settings → hold **Shift+Alt** and right-click **Add Account**). I didn't test this option, but it should work.
