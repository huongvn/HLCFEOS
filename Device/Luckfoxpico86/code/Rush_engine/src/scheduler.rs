use log::info;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;

#[allow(dead_code)]
pub async fn run_periodic<F>(_name: &str, interval_secs: u64, task_fn: F, running: Arc<AtomicBool>)
where
    F: Fn() + Send + Clone + 'static,
{
    let mut interval = tokio::time::interval(Duration::from_secs(interval_secs));
    interval.tick().await;

    while running.load(std::sync::atomic::Ordering::Relaxed) {
        interval.tick().await;
        let t = task_fn.clone();
        tokio::task::spawn_blocking(move || {
            t();
        })
        .await
        .ok();
    }
}

pub fn log_task(name: &str, interval_secs: u64) {
    info!("Added periodic task: {} (every {}s)", name, interval_secs);
}
