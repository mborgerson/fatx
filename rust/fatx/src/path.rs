use std::path::{Component, Path, PathBuf};

pub fn normalize_virtual_path<P: AsRef<Path>>(input: P) -> PathBuf {
    let mut stack: Vec<Component> = Vec::new();

    for comp in input.as_ref().components() {
        match comp {
            Component::RootDir | Component::Normal(_) => {
                stack.push(comp);
            }
            Component::ParentDir => {
                // Pop last normal component unless we're at RootDir
                if let Some(Component::Normal(_)) = stack.last() {
                    stack.pop();
                }
            }
            _ => {}
        }
    }

    if stack.is_empty() {
        stack.push(Component::RootDir);
    }

    // Reconstruct the normalized path
    let mut normalized = PathBuf::new();
    for comp in stack {
        normalized.push(comp.as_os_str());
    }

    normalized
}
