from .client import FceuxBridge
from .project import Project


def open_bridge(token_file, *, timeout=10.0):
    return FceuxBridge.from_token_file(token_file, timeout=timeout)


def open_project(root):
    return Project.open(root)


def open_session(token_file, project_root, *, timeout=10.0):
    bridge = open_bridge(token_file, timeout=timeout)
    project = open_project(project_root)
    return bridge, project
