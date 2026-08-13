# T-Deck Pro A7682E cellular guide

The `tdeck_pro_a7682e` firmware includes a text-mode **4G Phone** entry and a
**4G SETTINGS** shortcut within the existing **Setting** application. The
cellular modem is disabled by default, so the board does not keep the A7682E
powered unless it is explicitly enabled.

## Before first use

1. Insert an active voice-and-SMS SIM. For China Unicom, enable VoLTE for the
   line and make sure the account has no outbound-voice restriction.
2. Open **Setting** → **4G SETTINGS** → **4G SET**. Select **4G ON** and wait
   for the state line to show `READY`, SIM `READY`, and `NET`.
3. Leave APN empty for the operator default, or enter the APN and optional APN
   user/password supplied by the carrier. If SMS sending fails, enter the
   carrier's SMSC in `+<country-code><number>` form.

The modem state machine runs while the e-paper screen saver is active, so an
incoming call or cellular SMS is still processed after the display sleeps.

## Phone and SMS

From **4G Phone**:

- Enter a number and select **DIAL**. The trailing `;` required for an AT
  voice call is added by firmware.
- Incoming calls display `INCOMING` and the caller ID when the network
  supplies it. Select **ANSWER** or **HANG**.
- **SMS** provides a recipient and a single GSM text field, capped at 160
  characters. New messages update the latest sender/body on the SMS page.

`W`/`S` moves among controls. Press Enter on a text field to switch between
navigation and text editing; Escape leaves editing or returns to the menu.

## Email over the cellular data connection

Open **4G Phone** → **EMAIL** → **SET**. Configure the SMTP server, username,
password, sender and optionally a default recipient. `PORT` cycles 465, 587,
and 25; `TLS` cycles the modem's security mode values 0–2. Use an SMTP app
password rather than the password of the primary email account.

The firmware uses the modem's `CSMTPS*` command family. Availability depends
on the A7682E firmware variant and the SMTP provider's TLS/authentication
policy. When the module reports that the command is unsupported or the server
rejects authentication, the page reports `EMAIL REJECTED` / `Email failed` and
does not expose credentials in its status text.

APN and email settings—including passwords—are persisted only in the device's
local settings store. They are not compiled into the firmware and are not
written to project files. Treat physical access to the device as access to
those settings.
