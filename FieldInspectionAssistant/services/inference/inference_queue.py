"""Bounded work selection for the board's shared inference accelerator."""

from __future__ import annotations

from concurrent.futures import Future
from dataclasses import dataclass, field
from threading import Condition
from time import monotonic


@dataclass(eq=False)
class Work:
    kind: str
    payload: object
    created: float = field(default_factory=monotonic)
    preparation: Future | None = None


class InferenceQueue:
    def __init__(self):
        self._condition = Condition()
        self._work = []

    def add_job(self, job, payload, temporary_input=None, prepare=None):
        with self._condition:
            if sum(work.kind == "job" for work in self._work) >= 8:
                return False
            work = Work("job", (job, payload, temporary_input))
            if prepare is not None:
                work.preparation = prepare()
                work.preparation.add_done_callback(self._preparation_ready)
            self._work.append(work)
            self._condition.notify()
            return True

    def _preparation_ready(self, _future):
        with self._condition:
            self._condition.notify()

    def add_video(self, payload):
        with self._condition:
            self._work = [work for work in self._work
                          if work.kind != "video" or not work.payload[0].cancelled()]
            if any(work.kind == "video" for work in self._work):
                return None
            result = Future()
            self._work.append(Work("video", (result, payload)))
            self._condition.notify()
            return result

    def add_preload(self, model, allow_eviction=False):
        with self._condition:
            for work in self._work:
                if work.kind == "preload" and work.payload[0] == model:
                    if allow_eviction:
                        work.payload = (model, True)
                    return
            if sum(work.kind == "preload" for work in self._work) < 4:
                self._work.append(Work("preload", (model, allow_eviction)))
                self._condition.notify()

    def cancel_preloads(self):
        with self._condition:
            self._work = [work for work in self._work if work.kind != "preload"]

    def status(self):
        with self._condition:
            return (sum(work.kind == "job" and work.payload[0]["state"] == "queued" for work in self._work),
                    [work.payload[0] for work in self._work if work.kind == "preload"])

    def wait(self, timeout):
        with self._condition:
            return self._condition.wait_for(lambda: bool(self._ready()), timeout)

    def _ready(self):
        waiting_for_audio = any(work.preparation is not None and not work.preparation.done()
                                for work in self._work)
        return [work for work in self._work if
                (work.preparation is None or work.preparation.done())
                and (work.kind != "preload" or not waiting_for_audio)]

    def discard_stale_jobs(self, maximum_age):
        now = monotonic()
        with self._condition:
            stale = [work for work in self._work if work.kind == "job" and
                     (work.payload[0]["state"] != "queued" or now - work.created >= maximum_age)]
            for work in stale:
                self._work.remove(work)
            return stale

    def pop(self):
        now = monotonic()

        def rank(work):
            if work.kind == "job":
                job, payload, _ = work.payload
                priority = (0 if job["state"] != "queued" or now - work.created >= 3
                            else 1 if payload["mode"] == "asr" and (payload.get("online") or payload.get("realtime_audio"))
                            else 3)
            else:
                priority = 2 if work.kind == "video" else 4
            return priority, work.created

        with self._condition:
            while ready := self._ready():
                work = min(ready, key=rank)
                self._work.remove(work)
                if work.kind != "video" or work.payload[0].set_running_or_notify_cancel():
                    return work
            return None
