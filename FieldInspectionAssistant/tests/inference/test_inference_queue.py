from concurrent.futures import Future
import unittest
from unittest.mock import patch

from inference_queue import InferenceQueue


class InferenceQueueTests(unittest.TestCase):
    def setUp(self):
        self.queue = InferenceQueue()

    def test_interactive_priority_and_order(self):
        self.queue.add_preload("qwen35")
        self.queue.add_job({"state": "queued", "id": "text1"}, {"mode": "text"})
        self.queue.add_job({"state": "queued", "id": "text2"}, {"mode": "text"})
        self.queue.add_video("clip")
        self.queue.add_job({"state": "queued", "id": "voice"}, {"mode": "asr", "online": True})
        self.assertEqual(self.queue.pop().payload[0]["id"], "voice")
        self.assertEqual(self.queue.pop().kind, "video")
        self.assertEqual(self.queue.pop().payload[0]["id"], "text1")
        self.assertEqual(self.queue.pop().payload[0]["id"], "text2")
        self.assertEqual(self.queue.pop().payload, ("qwen35", False))

    def test_aged_foreground_job_overtakes_video(self):
        self.queue.add_job({"state": "queued"}, {"mode": "text"})
        self.queue.add_video("clip")
        with patch("inference_queue.monotonic", return_value=float("inf")):
            self.assertEqual(self.queue.pop().kind, "job")

    def test_deduplicates_warmup_and_drops_cancelled_video(self):
        self.queue.add_preload("qwen3_video")
        self.queue.add_preload("qwen3_video", True)
        old = self.queue.add_video("old")
        self.assertIsNone(self.queue.add_video("busy"))
        self.assertTrue(old.cancel())
        current = self.queue.add_video("current")
        self.assertEqual(self.queue.pop().payload, (current, "current"))
        self.assertFalse(current.cancel())
        self.assertEqual(self.queue.pop().payload, ("qwen3_video", True))
        self.assertIsNone(self.queue.pop())

    def test_preparing_audio_does_not_hold_accelerator_or_start_warmup(self):
        job = {"state": "queued", "id": "dictation"}
        preparation = Future()
        self.queue.add_job(job, {"mode": "asr", "realtime_audio": True}, prepare=lambda: preparation)
        self.queue.add_preload("qwen35")
        self.queue.add_video("clip")
        self.assertEqual(self.queue.pop().kind, "video")
        self.assertIsNone(self.queue.pop())
        preparation.set_result(None)
        self.assertEqual(self.queue.pop().payload[0], job)
        self.assertEqual(self.queue.pop().kind, "preload")

    def test_bounds_pending_jobs_and_reclaims_cancelled_entries(self):
        for i in range(8):
            self.assertTrue(self.queue.add_job({"id": i, "state": "queued"}, {"mode": "text"}))
        self.assertFalse(self.queue.add_job({"state": "queued"}, {"mode": "text"}))
        expired = self.queue.discard_stale_jobs(0)
        self.assertEqual(len(expired), 8)
        self.assertEqual(self.queue.status(), (0, []))
