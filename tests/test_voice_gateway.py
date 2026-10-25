#!/usr/bin/env python3
"""Offline gateway contract tests: no sockets, cloud calls, or real credentials."""

from __future__ import annotations

import base64
import hashlib
import hmac
from dataclasses import replace
import io
import json
import os
import socket
import sqlite3
import struct
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from services.voice_gateway import gateway as gw  # noqa: E402


TOKEN_A = "test-only-token-for-device-a-00000000"
TOKEN_B = "test-only-token-for-device-b-00000000"


def wav_bytes(value=0, samples=16000 * 8):
    pcm = bytes((value,)) * (2 * samples)
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
        16, 1, 1, 16000, 32000, 2, 16, b"data", len(pcm),
    ) + pcm


class MemorySocket:
    """Exercise the actual HTTP handler without requiring network permissions."""

    def __init__(self, raw):
        self.reader = io.BytesIO(raw)
        self.response = bytearray()

    def makefile(self, mode, *args):
        assert mode == "rb"
        return self.reader

    def settimeout(self, timeout):
        self.timeout = timeout

    def sendall(self, data):
        self.response.extend(data)


class GatewayTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="passport-gateway-test-")
        self.addCleanup(temporary.cleanup)
        self.database = Path(temporary.name) / "memory.sqlite3"
        self.settings = gw.Settings(
            tokens={"a": TOKEN_A, "b": TOKEN_B},
            deepseek_key="test-only-chat-key", asr_key="test-only-asr-key",
            database=self.database,
        )
        self.providers = Mock()
        self.providers.transcribe.return_value = "我叫小明。"
        self.providers.complete.return_value = "你好，小明。"
        self.gateway = gw.Gateway(self.settings, self.providers)
        self.wav = wav_bytes()

    def assert_error(self, status, code, callable_, *args):
        with self.assertRaises(gw.GatewayError) as caught:
            callable_(*args)
        self.assertEqual((caught.exception.status, caught.exception.code), (status, code))

    def request(self, method="GET", path="/health", body=b"", headers=None):
        if headers is None:
            headers = [("Authorization", "Bearer " + TOKEN_A)]
        wire = f"{method} {path} HTTP/1.1\r\nHost: localhost\r\n".encode()
        wire += b"".join(f"{name}: {value}\r\n".encode("latin-1") for name, value in headers)
        connection = MemorySocket(wire + b"\r\n" + body)
        gw.Handler(connection, ("127.0.0.1", 12345), SimpleNamespace(gateway=self.gateway))
        head, payload = bytes(connection.response).split(b"\r\n\r\n", 1)
        lines = head.decode().split("\r\n")
        status = int(lines[0].split()[1])
        response_headers = dict(line.split(": ", 1) for line in lines[1:])
        self.assertEqual(int(response_headers["Content-Length"]), len(payload))
        self.assertEqual(response_headers["Cache-Control"], "no-store")
        self.assertEqual(response_headers["Connection"], "close")
        self.assertLessEqual(len(payload), gw.MAX_RESPONSE_BYTES)
        return status, json.loads(payload)

    def voice_headers(self, *extra):
        return [
            ("Authorization", "Bearer " + TOKEN_A),
            ("Content-Type", "audio/wav"),
            ("Content-Length", str(len(self.wav))),
            *extra,
        ]

    def test_authentication_is_required_for_every_endpoint(self):
        for method, path in (("GET", "/health"), ("POST", "/v1/voice"), ("DELETE", "/v1/memory")):
            for headers in ([], [("Authorization", "Bearer incorrect")],
                            [("Authorization", "Bearer é")],
                            [("Authorization", "Basic " + TOKEN_A)],
                            [("Authorization", "Bearer " + TOKEN_A)] * 2):
                with self.subTest(method=method, headers=headers):
                    self.assertEqual(self.request(method, path, headers=headers)[0], 401)
        self.providers.transcribe.assert_not_called()

    def test_http_voice_health_delete_and_isolation(self):
        status, result = self.request("POST", "/v1/voice", self.wav, self.voice_headers(("X-Request-ID", "first")))
        self.assertEqual(status, 200)
        self.assertEqual(result, {"transcript": "我叫小明。", "reply": "你好，小明。", "turns": 1})
        self.assertEqual(self.request(), (200, {"ok": True, "turns": 1}))
        self.assertEqual(self.request(headers=[("Authorization", "Bearer " + TOKEN_B)]), (200, {"ok": True, "turns": 0}))
        self.assertEqual(self.request("DELETE", "/v1/memory"), (200, {"ok": True, "turns": 0}))
        self.assertEqual(self.request(), (200, {"ok": True, "turns": 0}))
        self.assertEqual(self.request("GET", "/unknown")[0], 404)

    def test_http_applies_selected_persona_and_utf8_name(self):
        encoded_name = base64.b64encode("星蓝".encode()).decode()
        status, _ = self.request(
            "POST", "/v1/voice", self.wav,
            self.voice_headers(("X-Request-ID", "profile"),
                               ("X-Assistant-Persona", "companion"),
                               ("X-Assistant-Name-B64", encoded_name)),
        )
        self.assertEqual(status, 200)
        messages = self.providers.complete.call_args.args[0]
        self.assertEqual(messages[0]["content"], gw.PERSONA_PROMPTS["companion"])
        self.assertIn("星蓝", messages[1]["content"])
        for headers in (
            self.voice_headers(("X-Assistant-Persona", "unknown")),
            self.voice_headers(("X-Assistant-Name-B64", "not-base64")),
            self.voice_headers(("X-Assistant-Name-B64", base64.b64encode("   ".encode()).decode())),
            self.voice_headers(("X-Assistant-Persona", "expert"),
                               ("X-Assistant-Persona", "companion")),
            self.voice_headers(("X-Assistant-Name", "a"), ("X-Assistant-Name", "b")),
        ):
            self.assertEqual(self.request("POST", "/v1/voice", self.wav, headers)[0], 400)
        # Compatibility header: unencoded or url-encoded X-Assistant-Name
        status, _ = self.request(
            "POST", "/v1/voice", self.wav,
            self.voice_headers(("X-Request-ID", "compat-name"),
                               ("X-Assistant-Name", "%E6%98%9F%E8%93%9D")),
        )
        self.assertEqual(status, 200)

    def test_enabled_bing_results_are_added_before_the_user_question(self):
        settings = replace(self.settings, bing_search_enabled=True)
        providers = Mock()
        providers.transcribe.return_value = "今天有什么新闻？"
        providers.plan_search.return_value = "今日 新闻"
        providers.search.return_value = [{
            "title": "新闻标题", "description": "新闻摘要", "url": "https://example.com/news",
        }]
        providers.complete.return_value = "这是回答。"
        gateway = gw.Gateway(settings, providers)
        result = gateway.voice("a", self.wav, "bing")
        self.assertEqual(result["reply"], "这是回答。")
        messages = providers.complete.call_args.args[0]
        self.assertEqual(messages[-1], {"role": "user", "content": "今天有什么新闻？"})
        self.assertIn("必应公开网页搜索结果", messages[-2]["content"])
        self.assertIn("https://example.com/news", messages[-2]["content"])
        providers.search.assert_called_once_with("今日 新闻")

    def test_failed_or_unneeded_plan_never_searches_raw_transcript(self):
        gateway = gw.Gateway(replace(self.settings, bing_search_enabled=True), self.providers)
        for index, query in enumerate((None, "")):
            self.providers.plan_search.return_value = query
            gateway.voice("a", self.wav, f"no-search-{index}")
        self.providers.search.assert_not_called()

    def test_http_refuses_unbounded_or_ambiguous_bodies(self):
        cases = [
            (411, [("Authorization", "Bearer " + TOKEN_A)]),
            (400, self.voice_headers(("Content-Length", str(len(self.wav))))),
            (400, self.voice_headers(("Transfer-Encoding", "chunked"))),
            (400, self.voice_headers(("X-Request-ID", "a"), ("X-Request-ID", "b"))),
            (415, self.voice_headers(("Content-Encoding", "gzip"))),
        ]
        for length in ("-1", "garbage", "99999999"):
            cases.append((400, [("Authorization", "Bearer " + TOKEN_A), ("Content-Length", length)]))
        for length in ("0", "44", str(len(self.wav) + 2)):
            cases.append((413, [("Authorization", "Bearer " + TOKEN_A), ("Content-Length", length)]))
        cases.append((415, [("Authorization", "Bearer " + TOKEN_A), ("Content-Length", str(len(self.wav))), ("Content-Type", "application/octet-stream")]))
        for status, headers in cases:
            with self.subTest(status=status, headers=headers):
                self.assertEqual(self.request("POST", "/v1/voice", headers=headers)[0], status)
        self.assertEqual(self.request("POST", "/v1/voice", self.wav[:-1], self.voice_headers())[0], 400)
        self.providers.transcribe.assert_not_called()

    def test_wav_requires_device_format_and_bounded_duration(self):
        gw.validate_wav(self.wav)
        gw.validate_wav(wav_bytes(samples=1))
        gw.validate_wav(wav_bytes(samples=16000))
        for offset, replacement in ((0, b"NOPE"), (4, struct.pack("<I", 12)),
                                    (16, struct.pack("<I", 18)), (20, b"\x03\x00"),
                                    (22, b"\x02\x00"), (24, struct.pack("<I", 8000)),
                                    (32, b"\x04\x00"), (34, b"\x08\x00"),
                                    (36, b"JUNK"), (40, struct.pack("<I", 4))):
            malformed = bytearray(self.wav)
            malformed[offset:offset + len(replacement)] = replacement
            self.assert_error(400, "invalid_wav", gw.validate_wav, bytes(malformed))
        self.assert_error(400, "invalid_wav", gw.validate_wav, self.wav[:-1])
        for body in (b"", self.wav[:44], self.wav + b"\x00\x00"):
            self.assert_error(413, "audio_size", gw.validate_wav, body)

    def test_context_is_persistent_ordered_and_per_device(self):
        self.gateway.voice("a", self.wav, "one")
        restarted = gw.Gateway(self.settings, self.providers)
        restarted.voice("a", self.wav, "two")
        self.assertEqual(self.providers.complete.call_args.args[0], [
            {"role": "system", "content": gw.SYSTEM_PROMPT},
            {"role": "system", "content": "你在本设备上的名字是“洛天依”。请以这个名字自称，不要改用其他名字。"},
            {"role": "system", "content": "本设备支持简短颜文字，例如(≧▽≦)、(｡･ω･｡)、(つ≧▽≦)つ、(╥﹏╥)、ʕ•ᴥ•ʔ。可按语气适度使用，避免彩色emoji。"},
            {"role": "user", "content": "我叫小明。"},
            {"role": "assistant", "content": "你好，小明。"},
            {"role": "user", "content": "我叫小明。"},
        ])
        restarted.voice("b", self.wav, "one")
        self.assertEqual(len(self.providers.complete.call_args.args[0]), 4)
        restarted.clear("a")
        self.assertEqual(restarted.memory.context("a"), [])
        self.assertEqual(restarted.memory.count("a"), 0)
        self.assertEqual(restarted.memory.count("b"), 1)
        if os.name != "nt":  # POSIX mode bits do not describe Windows ACLs.
            self.assertEqual(self.database.stat().st_mode & 0o777, 0o600)

    def test_idempotency_survives_restart_and_detects_audio_mismatch(self):
        result = self.gateway.voice("a", self.wav, "same-request")
        restarted = gw.Gateway(self.settings, self.providers)
        self.assertEqual(restarted.voice("a", self.wav, "same-request"), result)
        self.assertEqual(self.providers.transcribe.call_count, 1)
        self.assertEqual(self.providers.complete.call_count, 1)
        self.assert_error(409, "request_conflict", restarted.voice, "a", wav_bytes(1), "same-request")
        self.assertEqual(restarted.memory.count("a"), 1)
        restarted.clear("a")
        self.assertEqual(restarted.voice("a", wav_bytes(1), "same-request")["turns"], 1)

    def test_retention_and_context_are_bounded(self):
        memory = self.gateway.memory
        for number in range(1, gw.RETAINED_TURNS + 4):
            memory.append("a", str(number), "hash", f"u{number}", f"a{number}")
        self.assertEqual(memory.count("a"), gw.RETAINED_TURNS + 3)
        self.assertIsNone(memory.cached("a", "3", "hash"))
        self.assertEqual(memory.cached("a", "4", "hash")["turns"], 4)
        context = memory.context("a")
        self.assertEqual(len(context), gw.CONTEXT_TURNS * 2)
        self.assertEqual(context[0]["content"], "u120")
        self.assertEqual(context[-1]["content"], "a131")

    def test_failed_append_is_atomic(self):
        memory = self.gateway.memory
        memory.append("a", "same", "hash", "u", "a")
        with self.assertRaises(sqlite3.IntegrityError):
            memory.append("a", "same", "hash", "u", "a")
        self.assertEqual(memory.count("a"), 1)
        self.assertEqual(len(memory.context("a")), 2)

    def test_provider_failure_never_creates_partial_memory_and_unlocks(self):
        for failing in (self.providers.transcribe, self.providers.complete):
            failing.side_effect = gw.GatewayError(503, "test_failure", "Safe failure.")
            self.assert_error(503, "test_failure", self.gateway.voice, "a", self.wav, "retry")
            self.assertEqual(self.gateway.memory.count("a"), 0)
            self.assertEqual(self.gateway.memory.context("a"), [])
            self.assertFalse(self.gateway.locks["a"].locked())
            failing.side_effect = None
        self.assertEqual(self.gateway.voice("a", self.wav, "retry")["turns"], 1)

    def test_provider_text_and_encoded_json_limits(self):
        for text, status, code in ((" ", 422, "no_speech"), (None, 502, "asr_response"),
                                   ("中" * 257, 502, "asr_response"),
                                   ("a\x00b", 502, "asr_response"),
                                   ("\ud800", 502, "asr_response")):
            self.providers.transcribe.return_value = text
            self.assert_error(status, code, self.gateway.voice, "a", self.wav, "retry")
            self.assertEqual(self.gateway.memory.count("a"), 0)
        self.providers.transcribe.return_value = "中" * 256
        self.providers.complete.return_value = "中" * 512
        self.assertEqual(self.gateway.voice("a", self.wav, "limits")["turns"], 1)
        self.providers.complete.return_value = "中" * 513
        self.assert_error(502, "chat_response", self.gateway.voice, "a", self.wav, "over")
        self.providers.transcribe.return_value = "\x01" * gw.MAX_TRANSCRIPT_BYTES
        self.providers.complete.return_value = "\x01" * gw.MAX_REPLY_BYTES
        self.assert_error(502, "response_size", self.gateway.voice, "a", self.wav, "escaped")
        self.assertEqual(self.gateway.memory.count("a"), 1)

    def test_busy_device_rejects_voice_and_clear_but_other_devices_work(self):
        with self.gateway.locks["a"]:
            self.assert_error(409, "device_busy", self.gateway.voice, "a", self.wav, "busy")
            self.assert_error(409, "device_busy", self.gateway.clear, "a")
            self.assertEqual(self.gateway.voice("b", self.wav, "available")["turns"], 1)

    def test_invalid_request_ids_are_rejected_before_provider_call(self):
        for request_id in ("../other", "\r\nHeader", "a" * 65, "中文", "a b"):
            self.assert_error(400, "request_id", self.gateway.voice, "a", self.wav, request_id)
        self.providers.transcribe.assert_not_called()

    def test_http_reports_storage_failure_without_leaking_details(self):
        with patch.object(self.gateway.memory, "count", side_effect=sqlite3.OperationalError("PRIVATE_SQL_DETAIL")):
            status, body = self.request()
        self.assertEqual(status, 503)
        self.assertEqual(body["error"]["code"], "memory_unavailable")
        self.assertNotIn("PRIVATE", json.dumps(body))


class ConfigurationTest(unittest.TestCase):
    def setUp(self):
        self.env = {"VOICE_GATEWAY_TOKEN": TOKEN_A, "DEEPSEEK_API_KEY": "test-chat", "ASR_API_KEY": "test-asr", "TENCENT_SECRET_ID": "test-id"}

    def test_defaults_and_multi_device_config(self):
        settings = gw.Settings.from_env(self.env)
        self.assertEqual(settings.tokens, {"default": TOKEN_A})
        self.assertEqual(settings.host, "127.0.0.1")
        self.assertEqual(settings.deepseek_model, "deepseek-flash")
        self.assertEqual(settings.asr_model, "16k_zh")
        self.assertEqual(settings.tencent_secret_id, "test-id")
        self.assertFalse(settings.bing_search_enabled)
        enabled = gw.Settings.from_env({**self.env, "BING_SEARCH_ENABLED": "true"})
        self.assertTrue(enabled.bing_search_enabled)
        del self.env["VOICE_GATEWAY_TOKEN"]
        self.env["VOICE_GATEWAY_TOKENS"] = json.dumps({"one": TOKEN_A, "two": TOKEN_B})
        self.assertEqual(len(gw.Settings.from_env(self.env).tokens), 2)

    def test_rejects_missing_invalid_or_ambiguous_configuration(self):
        for key in self.env:
            invalid = dict(self.env)
            del invalid[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                gw.Settings.from_env(invalid)
        for key, value in (("VOICE_GATEWAY_TOKEN", "short"), ("ASR_API_KEY", "key\nInjected"),
                           ("VOICE_GATEWAY_PORT", "0"), ("VOICE_GATEWAY_PORT", "65536"),
                           ("VOICE_GATEWAY_PORT", "bad"),
                           ("VOICE_GATEWAY_TOKENS", json.dumps({"one": TOKEN_B}))):
            with self.subTest(key=key), self.assertRaises(ValueError):
                gw.Settings.from_env({**self.env, key: value})
        for tokens in ("[]", "{}", "not-json", json.dumps({"../bad": TOKEN_A}),
                       json.dumps({"one": TOKEN_A, "two": TOKEN_A})):
            env = {**self.env, "VOICE_GATEWAY_TOKENS": tokens}
            del env["VOICE_GATEWAY_TOKEN"]
            with self.subTest(tokens=tokens), self.assertRaises(ValueError):
                gw.Settings.from_env(env)

    def test_provider_urls_require_tls_except_localhost(self):
        for url in ("http://example.com", "https://u:p@example.com", "https://example.com?q=x",
                    "https://example.com#x", "https://example.com:bad", "https://example.com\n"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                gw.Settings.from_env({**self.env, "ASR_BASE_URL": url})
        for url in ("http://127.0.0.1:8000/v1", "http://[::1]:8000/v1", "https://example.com/v1/"):
            self.assertEqual(gw.Settings.from_env({**self.env, "ASR_BASE_URL": url}).asr_url, url.rstrip("/"))


class ProvidersTest(unittest.TestCase):
    def setUp(self):
        self.settings = gw.Settings({"a": TOKEN_A}, "test-chat", "test-asr", Path("unused"),
                                    asr_url="https://api.siliconflow.cn/v1", asr_model="FunAudioLLM/SenseVoiceSmall")
        self.opener = Mock()
        self.providers = gw.Providers(self.settings, self.opener)

    def response(self, value):
        body = value if isinstance(value, bytes) else gw._json_bytes(value)
        self.opener.open.return_value = io.BytesIO(body)

    def test_query_planning_keeps_context_and_rejects_malformed_output(self):
        history = [{"role": "user", "content": "查一下ESP32-C3"}]
        with patch.object(self.providers, "complete", return_value='{"query":"ESP32-C3 PSRAM 官方"}') as complete:
            self.assertEqual(self.providers.plan_search("它支持PSRAM吗", history), "ESP32-C3 PSRAM 官方")
            messages = complete.call_args.args[0]
            self.assertEqual(messages[-2], history[0])
            self.assertEqual(messages[-1]["content"], "它支持PSRAM吗")
        for value in ('{"query":""}', '{"query":"hello\\nInjected"}', 'not json',
                      '{"query":42}', json.dumps({"query": "中" * 65})):
            with patch.object(self.providers, "complete", return_value=value):
                expected = "" if value == '{"query":""}' else None
                self.assertEqual(self.providers.plan_search("question", history), expected)

    def test_asr_multipart_contains_model_and_wav(self):
        self.response({"text": "  测试语音  "})
        self.assertEqual(self.providers.transcribe(wav_bytes()), "测试语音")
        request = self.opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "https://api.siliconflow.cn/v1/audio/transcriptions")
        self.assertEqual(request.get_header("Authorization"), "Bearer test-asr")
        self.assertIn(b'filename="voice.wav"', request.data)
        self.assertIn(b"FunAudioLLM/SenseVoiceSmall", request.data)
        self.assertIn(wav_bytes(), request.data)
        self.assertEqual(self.opener.open.call_args.kwargs["timeout"], gw.PROVIDER_TIMEOUT)

    def test_tencent_signed_request_and_api_errors(self):
        settings = replace(self.settings, asr_url=gw.TENCENT_URL, asr_model="16k_zh",
                           tencent_secret_id="test-id")
        provider = gw.Providers(settings, self.opener)
        self.response({"Response": {"Result": "  测试语音  "}})
        with patch.object(gw.time, "time", return_value=1767225600):
            self.assertEqual(provider.transcribe(wav_bytes()), "测试语音")
        request = self.opener.open.call_args.args[0]
        self.assertEqual(request.full_url, gw.TENCENT_URL + "/")
        self.assertEqual(request.get_header("X-tc-action"), "SentenceRecognition")
        self.assertEqual(request.get_header("X-tc-version"), "2019-06-14")
        payload = json.loads(request.data)
        self.assertEqual(payload["EngSerViceType"], "16k_zh")
        self.assertEqual(payload["DataLen"], len(wav_bytes()))
        self.assertEqual(base64.b64decode(payload["Data"], validate=True), wav_bytes())
        # Independent canonical construction checks the sent body and header signature.
        canonical = "\n".join(("POST", "/", "", "content-type:application/json; charset=utf-8\nhost:asr.tencentcloudapi.com\n",
                                "content-type;host", hashlib.sha256(request.data).hexdigest()))
        key = hmac.new(b"TC3test-asr", b"2026-01-01", hashlib.sha256).digest()
        key = hmac.new(key, b"asr", hashlib.sha256).digest()
        key = hmac.new(key, b"tc3_request", hashlib.sha256).digest()
        text = "TC3-HMAC-SHA256\n1767225600\n2026-01-01/asr/tc3_request\n" + hashlib.sha256(canonical.encode()).hexdigest()
        signature = hmac.new(key, text.encode(), hashlib.sha256).hexdigest()
        self.assertTrue(request.get_header("Authorization").endswith("Signature=" + signature))
        for response in ({"Response": {"Error": {"Code": "AuthFailure", "Message": "PRIVATE"}}},
                         {}, {"Response": None}, {"Response": {"Result": ""}}):
            self.response(response)
            with self.assertRaises(gw.GatewayError) as caught:
                provider.transcribe(wav_bytes())
            self.assertNotIn("PRIVATE", str(caught.exception))

    def test_deepseek_request_and_finished_response(self):
        self.response({"choices": [{"finish_reason": "stop", "message": {"content": " answer "}}]})
        messages = [{"role": "user", "content": "question"}]
        self.assertEqual(self.providers.complete(messages), "answer")
        request = self.opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "https://api.deepseek.com/chat/completions")
        payload = json.loads(request.data)
        self.assertEqual(payload["messages"], messages)
        self.assertEqual(payload["thinking"], {"type": "disabled"})
        self.assertFalse(payload["stream"])
        self.assertEqual(payload["max_tokens"], 256)

    def test_bing_rss_search_is_bounded_and_does_not_send_api_credentials(self):
        self.response(b'''<?xml version="1.0"?><rss><channel>
          <item><title>  Result one  </title><description>Summary one</description>
          <link>https://example.com/one</link></item>
          <item><title>Result two</title><description>Summary two</description>
          <link>http://example.com/two</link></item>
        </channel></rss>''')
        results = self.providers.search("最新天气")
        self.assertEqual(results, [{"title": "Result one", "description": "Summary one",
                                    "url": "https://example.com/one"}])
        request = self.opener.open.call_args.args[0]
        self.assertIn("format=rss", request.full_url)
        self.assertIn("%E6%9C%80%E6%96%B0%E5%A4%A9%E6%B0%94", request.full_url)
        self.assertIsNone(request.get_header("Authorization"))

    def test_search_failures_degrade_to_no_results(self):
        self.opener.open.side_effect = urllib.error.URLError("private")
        self.assertEqual(self.providers.search("query"), [])

    def test_incomplete_or_malformed_answers_fail_closed(self):
        for payload in ({}, {"choices": []}, {"choices": [None]},
                        {"choices": [{"finish_reason": "length", "message": {"content": "partial"}}]},
                        {"choices": [{"finish_reason": "stop", "message": {"content": None}}]}):
            self.response(payload)
            with self.subTest(payload=payload), self.assertRaises(gw.GatewayError) as caught:
                self.providers.complete([])
            self.assertEqual(caught.exception.code, "chat_response")

    def test_upstream_errors_are_bounded_and_do_not_expose_bodies(self):
        for error, status in ((urllib.error.HTTPError("https://example.com", 429, "PRIVATE", {}, io.BytesIO(b"PRIVATE")), 503),
                              (urllib.error.HTTPError("https://example.com", 401, "PRIVATE", {}, io.BytesIO(b"PRIVATE")), 502),
                              (urllib.error.URLError("PRIVATE"), 502), (socket.timeout("PRIVATE"), 504)):
            self.opener.open.side_effect = error
            with self.subTest(error=type(error).__name__), self.assertRaises(gw.GatewayError) as caught:
                self.providers.transcribe(wav_bytes())
            self.assertEqual(caught.exception.status, status)
            self.assertNotIn("PRIVATE", str(caught.exception))
        self.opener.open.side_effect = None
        for body in (b"PRIVATE", b"[]", b"{", b"\xff", b"x" * (gw.MAX_PROVIDER_BYTES + 1)):
            self.response(body)
            with self.assertRaises(gw.GatewayError) as caught:
                self.providers.transcribe(wav_bytes())
            self.assertEqual(caught.exception.code, "asr_response")

    def test_redirect_handler_never_forwards_credentials(self):
        self.assertIsNone(gw._NoRedirect().redirect_request(None, None, 302, "", {}, "https://other.example"))


if __name__ == "__main__":
    unittest.main()
