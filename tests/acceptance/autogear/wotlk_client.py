#!/usr/bin/env python3
# This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
# information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
# or (at your option) any later version.
"""Minimal WotLK 3.3.5a (build 12340) test client for disposable local servers.

It does only what the AutoGear acceptance runner needs: SRP6 login on the authserver, the world
handshake with header encryption, character enum/create/login, chat (say, whisper, GM commands) and
the keep-alive answers a real client gives. It never connects anywhere but the host it is given, and
the runner only gives it 127.0.0.1.
"""

import hashlib
import hmac
import os
import socket
import struct
import time
import zlib

BUILD = 12340

# SRP6 parameters used by the AzerothCore authserver.
SRP_N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
SRP_G = 7
SRP_K = 3

# World opcodes (src/server/game/Server/Protocol/Opcodes.h).
CMSG_CHAR_CREATE = 0x036
CMSG_CHAR_ENUM = 0x037
SMSG_CHAR_CREATE = 0x03A
SMSG_CHAR_ENUM = 0x03B
CMSG_PLAYER_LOGIN = 0x03D
CMSG_LOGOUT_REQUEST = 0x04B
SMSG_LOGOUT_COMPLETE = 0x04D
CMSG_MESSAGECHAT = 0x095
SMSG_MESSAGECHAT = 0x096
SMSG_NOTIFICATION = 0x1CB
CMSG_PING = 0x1DC
SMSG_AUTH_CHALLENGE = 0x1EC
CMSG_AUTH_SESSION = 0x1ED
SMSG_AUTH_RESPONSE = 0x1EE
SMSG_LOGIN_VERIFY_WORLD = 0x236
SMSG_CHAT_PLAYER_NOT_FOUND = 0x2A9
SMSG_TIME_SYNC_REQ = 0x390
CMSG_TIME_SYNC_RESP = 0x391
SMSG_GM_MESSAGECHAT = 0x3B3

CHAT_MSG_SYSTEM = 0x00
CHAT_MSG_SAY = 0x01
CHAT_MSG_PARTY = 0x02
CHAT_MSG_WHISPER = 0x07
CHAT_MSG_WHISPER_INFORM = 0x09
LANG_UNIVERSAL = 0
LANG_COMMON = 7  # the runner's characters are Alliance humans

AUTH_OK = 0x0C
CHAR_CREATE_SUCCESS = 0x2F

SERVER_ENCRYPTION_KEY = bytes.fromhex("CC98AE04E897EACA12DDC09342915357")
SERVER_DECRYPTION_KEY = bytes.fromhex("C2B3723CC6AED9B5343C53EE2F4367CE")


def sha1(*parts):
    h = hashlib.sha1()
    for p in parts:
        h.update(p)
    return h.digest()


def le_int(data):
    return int.from_bytes(data, "little")


def le_bytes(value, size=32):
    return value.to_bytes(size, "little")


def make_registration(username, password, salt=None):
    """Salt and verifier in the form the account table stores (little-endian, 32 bytes each)."""
    salt = salt or os.urandom(32)
    inner = sha1(f"{username.upper()}:{password.upper()}".encode())
    x = le_int(sha1(salt, inner))
    return salt, le_bytes(pow(SRP_G, x, SRP_N))


def sha1_interleave(s):
    p = 0
    while p < len(s) and s[p] == 0:
        p += 1
    if p & 1:
        p += 1
    p //= 2
    even = sha1(bytes(s[2 * i] for i in range(16))[p:])
    odd = sha1(bytes(s[2 * i + 1] for i in range(16))[p:])
    return bytes(b for pair in zip(even, odd) for b in pair)


class Arc4:
    def __init__(self, key):
        s = list(range(256))
        j = 0
        for i in range(256):
            j = (j + s[i] + key[i % len(key)]) & 0xFF
            s[i], s[j] = s[j], s[i]
        self.s, self.i, self.j = s, 0, 0
        self.process(bytes(1024))  # ARC4-drop1024

    def process(self, data):
        s, i, j = self.s, self.i, self.j
        out = bytearray(len(data))
        for n, byte in enumerate(data):
            i = (i + 1) & 0xFF
            j = (j + s[i]) & 0xFF
            s[i], s[j] = s[j], s[i]
            out[n] = byte ^ s[(s[i] + s[j]) & 0xFF]
        self.i, self.j = i, j
        return bytes(out)


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return data


def auth_login(host, port, username, password, timeout=20.0):
    """SRP6 login on the authserver. Returns the 40-byte session key K."""
    user = username.upper().encode()
    with socket.create_connection((host, port), timeout=timeout) as sock:
        body = (b"WoW\0" + bytes([3, 3, 5]) + struct.pack("<H", BUILD) + b"68x\0" + b"niW\0" + b"SUne" +
                struct.pack("<I", 0) + bytes([127, 0, 0, 1]) + bytes([len(user)]) + user)
        sock.sendall(bytes([0x00, 0x08]) + struct.pack("<H", len(body)) + body)
        head = recv_exact(sock, 3)
        if head[0] != 0x00 or head[2] != 0x00:
            raise RuntimeError(f"logon challenge refused: {head.hex()}")
        b_pub = recv_exact(sock, 32)
        g_len = recv_exact(sock, 1)[0]
        g = le_int(recv_exact(sock, g_len))
        n_len = recv_exact(sock, 1)[0]
        n = le_int(recv_exact(sock, n_len))
        salt = recv_exact(sock, 32)
        recv_exact(sock, 16 + 1)  # version challenge, security flags (none for test accounts)
        if g != SRP_G or n != SRP_N:
            raise RuntimeError("unexpected SRP6 parameters")

        a = le_int(os.urandom(32)) % n
        a_pub = le_bytes(pow(g, a, n))
        u = le_int(sha1(a_pub, b_pub))
        x = le_int(sha1(salt, sha1(user + b":" + password.upper().encode())))
        b_int = le_int(b_pub)
        s = pow((b_int - SRP_K * pow(g, x, n)) % n, a + u * x, n)
        key = sha1_interleave(le_bytes(s))
        ng = bytes(p ^ q for p, q in zip(sha1(le_bytes(n)), sha1(bytes([g]))))
        m1 = sha1(ng, sha1(user), salt, a_pub, b_pub, key)
        sock.sendall(bytes([0x01]) + a_pub + m1 + bytes(20) + bytes([0, 0]))
        proof = recv_exact(sock, 2)
        if proof[0] != 0x01 or proof[1] != 0x00:
            raise RuntimeError(f"logon proof refused: {proof.hex()}")
        recv_exact(sock, 20 + 4 + 4 + 2)  # M2 and flags; the server already accepted M1
        return key


class ChatMessage:
    def __init__(self, msg_type, sender_guid, text, sender_name=""):
        self.type = msg_type
        self.sender_guid = sender_guid
        self.text = text
        self.sender_name = sender_name

    def __repr__(self):
        return f"ChatMessage(type={self.type}, sender={self.sender_guid:#x}, text={self.text!r})"


class WorldClient:
    """One connected game client: a real, non-headless session."""

    def __init__(self, host, port, username, session_key, timeout=30.0, log=None):
        self.username = username.upper()
        self.key = session_key
        self.log = log or (lambda line: None)
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.encrypt = None
        self.decrypt = None
        self.inbox = []  # (opcode, payload) not consumed by a waiter
        self.chat = []   # every ChatMessage received, in order
        self.player_guid = 0
        self._handshake()

    # --- framing ---------------------------------------------------------------------------------
    def send(self, opcode, payload=b""):
        header = struct.pack(">H", len(payload) + 4) + struct.pack("<I", opcode)
        if self.encrypt:
            header = self.encrypt.process(header)
        self.sock.sendall(header + payload)

    def _recv_packet(self):
        first = recv_exact(self.sock, 1)
        if self.decrypt:
            first = self.decrypt.process(first)
        size_len = 3 if first[0] & 0x80 else 2
        rest = recv_exact(self.sock, size_len + 2 - 1)
        if self.decrypt:
            rest = self.decrypt.process(rest)
        header = first + rest
        if size_len == 3:
            size = ((header[0] & 0x7F) << 16) | (header[1] << 8) | header[2]
        else:
            size = (header[0] << 8) | header[1]
        opcode = struct.unpack("<H", header[size_len:size_len + 2])[0]
        return opcode, recv_exact(self.sock, size - 2)

    def _handle_background(self, opcode, payload):
        if opcode == SMSG_TIME_SYNC_REQ:
            counter = struct.unpack("<I", payload[:4])[0]
            self.send(CMSG_TIME_SYNC_RESP, struct.pack("<II", counter, int(time.monotonic() * 1000) & 0xFFFFFFFF))
            return True
        if opcode in (SMSG_MESSAGECHAT, SMSG_GM_MESSAGECHAT):
            message = parse_chat(payload, opcode == SMSG_GM_MESSAGECHAT)
            if message:
                self.chat.append(message)
                self.log(f"chat {message.type} {message.sender_guid:#x}: {message.text}")
            return True
        if opcode == SMSG_NOTIFICATION:
            text = payload.split(b"\0", 1)[0].decode(errors="replace")
            self.chat.append(ChatMessage(-1, 0, text))
            self.log(f"notification: {text}")
            return True
        return False

    def pump(self, seconds):
        """Read packets for a while, answering keep-alives and collecting chat."""
        deadline = time.monotonic() + seconds
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self.sock.settimeout(remaining)
            try:
                opcode, payload = self._recv_packet()
            except socket.timeout:
                return
            if not self._handle_background(opcode, payload):
                self.inbox.append((opcode, payload))

    def wait_for(self, opcodes, timeout=30.0):
        if isinstance(opcodes, int):
            opcodes = (opcodes,)
        for index, (opcode, payload) in enumerate(self.inbox):
            if opcode in opcodes:
                del self.inbox[index]
                return opcode, payload
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sock.settimeout(max(0.1, deadline - time.monotonic()))
            try:
                opcode, payload = self._recv_packet()
            except socket.timeout:
                break
            if opcode in opcodes:
                return opcode, payload
            if not self._handle_background(opcode, payload):
                self.inbox.append((opcode, payload))
        raise TimeoutError(f"no opcode {[hex(o) for o in opcodes]} within {timeout}s")

    # --- session ---------------------------------------------------------------------------------
    def _handshake(self):
        opcode, payload = self._recv_packet()
        if opcode != SMSG_AUTH_CHALLENGE:
            raise RuntimeError(f"expected SMSG_AUTH_CHALLENGE, got {opcode:#x}")
        server_seed = payload[4:8]
        local_challenge = os.urandom(4)
        account = self.username.encode()
        digest = sha1(account, bytes(4), local_challenge, server_seed, self.key)
        body = (struct.pack("<II", BUILD, 0) + account + b"\0" + struct.pack("<I", 0) + local_challenge +
                struct.pack("<IIIQ", 0, 0, 1, 0) + digest + struct.pack("<I", 0))
        self.send(CMSG_AUTH_SESSION, body)
        self.encrypt = Arc4(hmac.new(SERVER_DECRYPTION_KEY, self.key, hashlib.sha1).digest())
        self.decrypt = Arc4(hmac.new(SERVER_ENCRYPTION_KEY, self.key, hashlib.sha1).digest())
        _, payload = self.wait_for(SMSG_AUTH_RESPONSE)
        if payload[0] != AUTH_OK:
            raise RuntimeError(f"world auth refused: {payload[0]:#x}")

    def enum_characters(self):
        self.send(CMSG_CHAR_ENUM)
        _, payload = self.wait_for(SMSG_CHAR_ENUM)
        count = payload[0]
        pos = 1
        chars = []
        for _ in range(count):
            guid = struct.unpack_from("<Q", payload, pos)[0]
            pos += 8
            end = payload.index(b"\0", pos)
            name = payload[pos:end].decode()
            pos = end + 1
            race, cls, gender = payload[pos], payload[pos + 1], payload[pos + 2]
            pos += 3 + 5  # skin, face, hairStyle, hairColor, facialHair
            level = payload[pos]
            pos += 1
            pos += 4 + 4 + 4 * 3 + 4 + 4 + 4 + 1  # zone, map, x/y/z, guild, flags, customize, first login
            pos += 4 + 4 + 4  # pet display, level, family
            pos += 23 * (4 + 1 + 4)  # equipment and bags
            chars.append({"guid": guid, "name": name, "race": race, "class": cls, "gender": gender, "level": level})
        return chars

    def create_character(self, name, race, cls, gender=0):
        self.send(CMSG_CHAR_CREATE, name.encode() + b"\0" + bytes([race, cls, gender, 0, 0, 0, 0, 0, 0]))
        _, payload = self.wait_for(SMSG_CHAR_CREATE)
        return payload[0]

    def login(self, guid, settle=5.0):
        # The server only accepts a character listed by this session's own character enum.
        if guid not in {c["guid"] for c in self.enum_characters()}:
            raise RuntimeError(f"character {guid} is not on this account")
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", guid))
        self.wait_for(SMSG_LOGIN_VERIFY_WORLD, timeout=60.0)
        self.player_guid = guid
        self.pump(settle)

    def logout(self):
        self.send(CMSG_LOGOUT_REQUEST)
        try:
            self.wait_for(SMSG_LOGOUT_COMPLETE, timeout=30.0)
        except TimeoutError:
            pass

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    # --- chat ------------------------------------------------------------------------------------
    def say(self, text):
        self.send(CMSG_MESSAGECHAT, struct.pack("<II", CHAT_MSG_SAY, LANG_COMMON) + text.encode() + b"\0")

    def command(self, text, wait=2.0):
        """Run a GM command (sent as say text starting with '.'); returns the system lines it printed."""
        start = len(self.chat)
        self.say(text)
        self.pump(wait)
        return [m.text for m in self.chat[start:] if m.type == CHAT_MSG_SYSTEM]

    def whisper(self, target, text):
        self.send(CMSG_MESSAGECHAT, struct.pack("<II", CHAT_MSG_WHISPER, LANG_COMMON) + target.encode() + b"\0" +
                  text.encode() + b"\0")

    def whispers_since(self, start, sender_guid=None):
        return [m for m in self.chat[start:]
                if m.type == CHAT_MSG_WHISPER and (sender_guid is None or m.sender_guid == sender_guid)]


def parse_chat(payload, gm_message):
    try:
        msg_type = payload[0]
        sender = struct.unpack_from("<Q", payload, 5)[0]
        pos = 1 + 4 + 8 + 4
        name = ""
        if msg_type in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x29, 0x2A, 0x2B, 0x2C) or gm_message:
            length = struct.unpack_from("<I", payload, pos)[0]
            pos += 4
            name = payload[pos:pos + length - 1].decode(errors="replace")
            pos += length
            if msg_type in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x29, 0x2A, 0x2B, 0x2C):
                pos += 8
                return None  # creature speech is not needed by the runner
        if msg_type == 0x11:  # channel
            end = payload.index(b"\0", pos)
            pos = end + 1
        pos += 8  # receiver guid
        length = struct.unpack_from("<I", payload, pos)[0]
        pos += 4
        text = payload[pos:pos + length - 1].decode(errors="replace")
        return ChatMessage(msg_type, sender, text, name)
    except (IndexError, struct.error, ValueError):
        return None
