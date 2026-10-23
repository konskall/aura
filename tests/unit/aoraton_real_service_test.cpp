#include "aura/aoraton_real.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void push(aura::AoratonMailbox& mailbox, aura::AoratonEvent event, const char* message) {
    expect(mailbox.push_event(std::move(event)), message);
}

}  // namespace

int main() {
    using namespace aura;

    AoratonMailbox mailbox;
    int next_id = 1;
    RealAoratonService service(mailbox, AoratonRuntimeMode::IntegrationTest, [&] {
        return std::string("client-") + std::to_string(next_id++);
    });
    expect(!service.snapshot().mock_mode, "real service never identifies as mock");
    expect(service.snapshot().runtime_mode == AoratonRuntimeMode::IntegrationTest, "test mode is explicit");
    expect(!service.connect(), "unpaired service cannot connect");
    expect(service.snapshot().last_error == "PAIRING_REQUIRED", "pairing requirement is truthful");

    expect(service.begin_pairing(), "unpaired service starts explicit pairing");
    AoratonCommand command;
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::BeginPairing,
           "pairing start crosses typed mailbox");
    AoratonEvent challenge;
    challenge.type = AoratonEventType::PairingChallenge;
    challenge.pairing_qr_payload = "https://test.invalid/pair#v=1&id=synthetic&pk=public-only";
    challenge.pairing_code = "TEST-4821";
    challenge.pairing_seconds_remaining = 2;
    push(mailbox, std::move(challenge), "pairing challenge queued");
    service.tick(1000);
    expect(service.snapshot().pairing == AoratonPairingState::WaitingForBrowser,
           "public pairing challenge becomes visible");
    expect(service.snapshot().pairing_qr_payload.find("public-only") != std::string::npos,
           "QR payload carries public parameters only");
    expect(service.snapshot().pairing_seconds_remaining == 1, "pairing expiry countdown advances");

    AoratonEvent sas;
    sas.type = AoratonEventType::PairingAwaitingSas;
    sas.pairing_sas = "204 681";
    sas.pairing_seconds_remaining = 90;
    push(mailbox, std::move(sas), "SAS event queued");
    service.tick(1);
    expect(service.snapshot().pairing == AoratonPairingState::AwaitingSasConfirmation,
           "pairing waits for human SAS confirmation");
    expect(service.confirm_pairing_sas(), "user can confirm visible SAS");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::ConfirmPairingSas,
           "SAS confirmation crosses typed mailbox");

    AoratonEvent paired;
    paired.type = AoratonEventType::Paired;
    paired.account_label = u8"\u0394\u03bf\u03ba\u03b9\u03bc\u03ae AURA";
    push(mailbox, std::move(paired), "paired event queued");
    service.tick(1);
    expect(service.snapshot().pairing == AoratonPairingState::Paired, "paired event authorizes service");
    expect(service.connect(), "paired service starts connection");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::Connect,
           "connect intent crosses typed mailbox");

    AoratonEvent connected;
    connected.type = AoratonEventType::ConnectionChanged;
    connected.connection = AoratonConnectionState::Connected;
    connected.realtime_active = true;
    push(mailbox, std::move(connected), "connection event queued");
    service.tick(1);
    expect(service.snapshot().connection == AoratonConnectionState::Connected, "worker reports connected");
    expect(service.snapshot().realtime_active, "realtime capability is explicit");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::RefreshRooms,
           "connection triggers authorized room refresh");

    AoratonEvent rooms;
    rooms.type = AoratonEventType::AuthorizedRooms;
    rooms.timestamp = "2026-10-21T08:30:00.000Z";
    rooms.rooms = {
        {"v2.room-one", "Room One", "hello", 2, true, true, true, false, "2026-10-21T08:29:00.000Z"},
        {"v2.room-locked", "Locked", "must-not-leak", 1, true, true, false, false, ""},
        {"v2.room-two", "Room Two", "", 0, true, true, true, false, ""},
        {"v2.not-authorized", "Hidden", "", 0, true, false, true, false, ""},
    };
    push(mailbox, std::move(rooms), "rooms event queued");
    service.tick(1);
    expect(service.snapshot().rooms.size() == 3, "only authorized rooms enter snapshot");
    expect(service.snapshot().rooms[1].preview.empty(), "room without key never exposes preview");
    expect(!service.select_room("v2.room-locked"), "room without key stays locked");
    expect(service.snapshot().last_error == "ROOM_KEY_REQUIRED", "locked state is distinct");
    expect(service.select_room("v2.room-one"), "authorized keyed room opens");
    expect(service.snapshot().rooms[0].unread == 0, "open room clears local unread");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::FetchRecentMessages,
           "room selection fetches bounded history");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::MarkRoomRead,
           "room selection updates per-device baseline");

    expect(service.prepare_send("v2.room-one", u8"\u039a\u03b1\u03bb\u03b7\u03bc\u03ad\u03c1\u03b1 \U0001f44b"),
           "valid UTF-8 text reaches confirmation");
    expect(service.snapshot().awaiting_confirmation, "explicit confirmation remains mandatory");
    expect(service.confirm_send(), "confirmed text is queued to worker");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::SendText,
           "confirmed send emits typed command");
    expect(command.room_id == "v2.room-one" && command.client_message_id == "client-1",
           "destination and stable id are preserved");
    expect(command.plaintext == u8"\u039a\u03b1\u03bb\u03b7\u03bc\u03ad\u03c1\u03b1 \U0001f44b", "worker receives exact confirmed text");
    command.clear_plaintext();
    expect(service.snapshot().messages.back().delivery == AoratonDelivery::Queued,
           "UI does not claim sent before transport result");

    AoratonEvent submitted;
    submitted.type = AoratonEventType::SendSubmitted;
    submitted.client_message_id = "client-1";
    push(mailbox, std::move(submitted), "submitted event queued");
    AoratonEvent acknowledged;
    acknowledged.type = AoratonEventType::SendAcknowledged;
    acknowledged.client_message_id = "client-1";
    acknowledged.server_message_id = "server-message-1";
    acknowledged.timestamp = "2026-10-21T08:31:00.000Z";
    push(mailbox, std::move(acknowledged), "ack event queued");
    service.tick(1);
    expect(service.snapshot().messages.back().delivery == AoratonDelivery::Acknowledged,
           "server response produces acknowledged state");
    expect(service.snapshot().messages.back().id == "server-message-1", "server id reconciles optimistic row");

    AoratonEvent incoming;
    incoming.type = AoratonEventType::IncomingMessage;
    incoming.timestamp = "2026-10-21T08:32:00.000Z";
    incoming.message = {"server-in-1", "v2.room-two", "Browser test", u8"\u0393\u03b5\u03b9\u03b1 \u03b1\u03c0\u03cc browser",
                        "2026-10-21T08:32:00.000Z", AoratonDirection::Incoming,
                        AoratonDelivery::Received, "browser-client-1"};
    push(mailbox, incoming, "incoming event queued");
    push(mailbox, std::move(incoming), "duplicate incoming queued");
    service.tick(1);
    expect(service.snapshot().rooms[2].unread == 1, "background incoming increments unread once");
    expect(service.snapshot().notifications.size() == 1, "incoming produces one deduplicated notification");
    expect(service.snapshot().notifications.front().preview.empty(), "notification preview is private by default");

    AoratonEvent locked_message;
    locked_message.type = AoratonEventType::IncomingMessage;
    locked_message.message = {"server-locked-1", "v2.room-locked", "Peer", "plaintext must be rejected", "",
                              AoratonDirection::Incoming, AoratonDelivery::Received, ""};
    push(mailbox, std::move(locked_message), "locked message event queued");
    const auto before_locked = service.snapshot().messages.size();
    service.tick(1);
    expect(service.snapshot().messages.size() == before_locked, "message without provisioned key is never displayed");

    service.set_network_available(false);
    expect(service.prepare_send("v2.room-one", "offline confirmation"), "offline draft may be reviewed");
    expect(!service.confirm_send(), "offline confirmation does not claim submission");
    expect(service.cancel_send(), "offline pending text can be cancelled");

    AoratonEvent revoked;
    revoked.type = AoratonEventType::SessionRevoked;
    push(mailbox, std::move(revoked), "revocation event queued");
    service.tick(1);
    expect(service.snapshot().pairing == AoratonPairingState::ReauthorizationRequired,
           "revoked session requires explicit reauthorization");
    expect(service.snapshot().rooms.empty() && service.snapshot().messages.empty(),
           "revocation removes room and plaintext state");
    expect(service.unpair(), "revoked device can erase its local grant");
    expect(mailbox.pop_command(command) && command.type == AoratonCommandType::Unpair,
           "unpair requests worker-side vault erasure");
    expect(service.snapshot().pairing == AoratonPairingState::NotPaired,
           "unpair returns to truthful not-paired state");

    {
        AoratonMailbox order_mailbox;
        int order_id = 1;
        RealAoratonService ordered(order_mailbox, AoratonRuntimeMode::IntegrationTest, [&] {
            return std::string("order-") + std::to_string(order_id++);
        });
        AoratonEvent pair_event;
        pair_event.type = AoratonEventType::Paired;
        push(order_mailbox, std::move(pair_event), "order paired queued");
        ordered.tick(1);
        expect(ordered.connect(), "order service connects");
        AoratonEvent online;
        online.type = AoratonEventType::ConnectionChanged;
        online.connection = AoratonConnectionState::Connected;
        push(order_mailbox, std::move(online), "order connection queued");
        ordered.tick(1);
        AoratonEvent order_rooms;
        order_rooms.type = AoratonEventType::AuthorizedRooms;
        order_rooms.rooms = {{"v2.order", "Order", "", 0, true, true, true, false, ""}};
        push(order_mailbox, std::move(order_rooms), "order rooms queued");
        ordered.tick(1);
        expect(ordered.select_room("v2.order"), "order room opens");
        const auto history_row = [](int index) {
            char stamp[40];
            std::snprintf(stamp, sizeof(stamp), "2026-10-21T08:%02d:%02d.000Z", index / 60, index % 60);
            return AoratonMessage{"history-" + std::to_string(index), "v2.order", "Peer", "history", stamp,
                                  AoratonDirection::Incoming, AoratonDelivery::Received, ""};
        };
        for (int batch = 0; batch < 99; batch += 33) {
            AoratonEvent history;
            history.type = AoratonEventType::MessageHistory;
            for (int index = batch; index < batch + 33; ++index) history.messages.push_back(history_row(index));
            push(order_mailbox, std::move(history), "history page queued");
            ordered.tick(1);
        }
        expect(ordered.prepare_send("v2.order", "pending") && ordered.confirm_send(), "order send queued");
        AoratonEvent late_incoming;
        late_incoming.type = AoratonEventType::IncomingMessage;
        late_incoming.message = history_row(200);
        push(order_mailbox, std::move(late_incoming), "incoming before ack queued");
        ordered.tick(1);
        expect(ordered.snapshot().messages.back().client_message_id == "order-1",
               "unacknowledged send stays newest and is not evicted by incoming rows");
        AoratonEvent order_ack;
        order_ack.type = AoratonEventType::SendAcknowledged;
        order_ack.client_message_id = "order-1";
        order_ack.server_message_id = "order-server";
        order_ack.timestamp = "2026-10-21T09:00:00.000Z";
        push(order_mailbox, std::move(order_ack), "late ack queued");
        ordered.tick(1);
        expect(ordered.snapshot().messages.back().id == "order-server" &&
               ordered.snapshot().messages.back().delivery == AoratonDelivery::Acknowledged,
               "late acknowledgement reconciles the retained send");
    }

    {
        // Room keys for the signed-in account: the passphrase and the invite only
        // travel inside a command, and the snapshot reports state, never secrets.
        AoratonMailbox key_mailbox;
        RealAoratonService keys(key_mailbox, AoratonRuntimeMode::Live, [] { return std::string("k-1"); });
        expect(!keys.unlock_keys("synthetic passphrase") && keys.snapshot().last_error == "PAIRING_REQUIRED",
               "unlock requires a signed-in account");
        AoratonEvent paired;
        paired.type = AoratonEventType::Paired;
        paired.account_label = "owner@example.com";
        push(key_mailbox, std::move(paired), "sign-in queued");
        keys.tick(1);
        expect(!keys.unlock_keys(""), "empty passphrase refused");
        expect(keys.unlock_keys("synthetic passphrase"), "unlock enqueued");
        expect(keys.snapshot().key_state == AoratonKeyState::Unlocking, "unlocking shown");
        expect(!keys.add_invite_key("#key=x&k=y") && keys.snapshot().last_error == "KEYS_BUSY",
               "one key operation at a time");
        AoratonCommand unlock;
        expect(key_mailbox.pop_command(unlock) && unlock.type == AoratonCommandType::UnlockVault &&
                   unlock.plaintext == "synthetic passphrase",
               "passphrase crosses the mailbox inside the command");
        unlock.clear_plaintext();
        expect(unlock.plaintext.empty(), "command wipes the passphrase");
        AoratonEvent changed;
        changed.type = AoratonEventType::KeysChanged;
        changed.key_state = AoratonKeyState::Ready;
        changed.key_count = 3;
        push(key_mailbox, std::move(changed), "keys changed queued");
        keys.tick(1);
        expect(keys.snapshot().key_state == AoratonKeyState::Ready && keys.snapshot().key_count == 3,
               "key state and count reported");
        expect(keys.add_invite_key("https://aoraton.com/#room=x&key=v2.Q&k=synthetic"), "invite enqueued");
        AoratonCommand invite;
        expect(key_mailbox.pop_command(invite) && invite.type == AoratonCommandType::AddInviteKey, "invite command");
        AoratonEvent revoked;
        revoked.type = AoratonEventType::SessionRevoked;
        push(key_mailbox, std::move(revoked), "revocation queued");
        keys.tick(1);
        expect(keys.snapshot().key_state == AoratonKeyState::Unknown && keys.snapshot().key_count == 0,
               "revocation resets key state");
    }

    AoratonMailbox bounded;
    for (std::size_t index = 0; index < AoratonMailbox::kMaxEvents; ++index) {
        AoratonEvent event;
        expect(bounded.push_event(std::move(event)), "event queue accepts bounded capacity");
    }
    AoratonEvent overflow;
    expect(!bounded.push_event(std::move(overflow)), "event queue rejects overflow");

    std::cout << "aoraton_real_service_test: PASS\n";
    return 0;
}
