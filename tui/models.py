from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional, List, Dict, Any


@dataclass
class Capabilities:
    user_namespaces: bool = False
    uts_namespace: bool = False
    pid_namespace: bool = False
    mount_namespace: bool = False
    cgroups_v2: bool = False
    reason: str = ""
    granular: Dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Capabilities":
        return cls(
            user_namespaces=bool(data.get("user_namespaces", False)),
            uts_namespace=bool(data.get("uts_namespace", False)),
            pid_namespace=bool(data.get("pid_namespace", False)),
            mount_namespace=bool(data.get("mount_namespace", False)),
            cgroups_v2=bool(data.get("cgroups_v2", False)),
            reason=data.get("reason", ""),
            granular=data.get("granular", {}),
        )


@dataclass
class ResourceLimits:
    cpu_percent: int = 0
    memory_max_bytes: int = 0
    pids_max: int = 0


@dataclass
class HostMetrics:
    valid: bool = False
    cpu_percent: float = 0.0
    mem_total_bytes: int = 0
    mem_avail_bytes: int = 0
    mem_used_bytes: int = 0
    load_1m: float = 0.0
    load_5m: float = 0.0
    load_15m: float = 0.0

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "HostMetrics":
        return cls(
            valid=bool(data.get("valid", False)),
            cpu_percent=float(data.get("cpu_percent", 0.0)),
            mem_total_bytes=int(data.get("mem_total_bytes", 0)),
            mem_avail_bytes=int(data.get("mem_avail_bytes", 0)),
            mem_used_bytes=int(data.get("mem_used_bytes", 0)),
            load_1m=float(data.get("load_1m", 0.0)),
            load_5m=float(data.get("load_5m", 0.0)),
            load_15m=float(data.get("load_15m", 0.0)),
        )


@dataclass
class WorkloadSummary:
    id: str
    name: str
    pid: int
    state: str
    active: bool = False
    cpu_percent: float = 0.0
    memory_bytes: int = 0
    pids_count: int = 0

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "WorkloadSummary":
        return cls(
            id=data.get("id", ""),
            name=data.get("name", ""),
            pid=int(data.get("pid", 0)),
            state=data.get("state", "UNKNOWN"),
            active=bool(data.get("active", False)),
            cpu_percent=float(data.get("cpu_percent", 0.0)),
            memory_bytes=int(data.get("memory_bytes", 0)),
            pids_count=int(data.get("pids_count", 0)),
        )


@dataclass
class WorkloadDetail:
    id: str
    name: str
    state: str
    pid: int
    supervisor_pid: int
    executable: str
    args: str
    work_dir: str
    created_at_ms: int
    started_at_ms: int
    finished_at_ms: int
    exit_code: int
    term_signal: int
    exec_errno: int
    error_msg: str
    log_file: str
    iso_uts: bool
    iso_hostname: str
    iso_pid: bool
    iso_mount: bool
    limits: ResourceLimits
    cgroup_path: str
    cgroup_active: bool

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "WorkloadDetail":
        lim_dict = data.get("limits", {})
        limits = ResourceLimits(
            cpu_percent=int(lim_dict.get("cpu_percent", 0)),
            memory_max_bytes=int(lim_dict.get("memory_max_bytes", 0)),
            pids_max=int(lim_dict.get("pids_max", 0)),
        )
        return cls(
            id=data.get("id", ""),
            name=data.get("name", ""),
            state=data.get("state", "UNKNOWN"),
            pid=int(data.get("pid", 0)),
            supervisor_pid=int(data.get("supervisor_pid", 0)),
            executable=data.get("executable", ""),
            args=data.get("args", ""),
            work_dir=data.get("work_dir", ""),
            created_at_ms=int(data.get("created_at_ms", 0)),
            started_at_ms=int(data.get("started_at_ms", 0)),
            finished_at_ms=int(data.get("finished_at_ms", 0)),
            exit_code=int(data.get("exit_code", 0)),
            term_signal=int(data.get("term_signal", 0)),
            exec_errno=int(data.get("exec_errno", 0)),
            error_msg=data.get("error_msg", ""),
            log_file=data.get("log_file", ""),
            iso_uts=bool(data.get("iso_uts", False)),
            iso_hostname=data.get("iso_hostname", ""),
            iso_pid=bool(data.get("iso_pid", False)),
            iso_mount=bool(data.get("iso_mount", False)),
            limits=limits,
            cgroup_path=data.get("cgroup_path", ""),
            cgroup_active=bool(data.get("cgroup_active", False)),
        )


@dataclass
class EventItem:
    timestamp: str
    level: str  # INFO, WARN, ERROR, SUCCESS
    message: str

    @classmethod
    def create(cls, level: str, message: str) -> "EventItem":
        now = datetime.now().strftime("%H:%M:%S")
        return cls(timestamp=now, level=level.upper(), message=message)
