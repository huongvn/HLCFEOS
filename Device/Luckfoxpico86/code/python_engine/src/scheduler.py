"""
Scheduler - Time-based scheduling for periodic tasks
"""

import schedule
import time
import threading
import logging
from typing import Callable

logger = logging.getLogger(__name__)


class Scheduler:
    """Manage periodic tasks"""
    
    def __init__(self):
        """Initialize Scheduler"""
        self.tasks = []
        self.running = False
        self.thread: Optional[threading.Thread] = None
        logger.info("Scheduler initialized")
    
    def add_periodic_task(self, interval: int, task_func: Callable, name: str = None):
        """
        Add periodic task
        
        Args:
            interval: Interval in seconds
            task_func: Task function to execute
            name: Task name for logging (optional)
        """
        schedule.every(interval).seconds.do(task_func)
        self.tasks.append({
            'name': name or task_func.__name__,
            'interval': interval,
            'func': task_func
        })
        logger.info(f"Added periodic task: {name or task_func.__name__} (every {interval}s)")
    
    def start(self):
        """Start scheduler in background thread"""
        if self.running:
            logger.warning("Scheduler is already running")
            return
        
        self.running = True
        self.thread = threading.Thread(target=self._run, daemon=True, name="SchedulerThread")
        self.thread.start()
        logger.info("Scheduler started")
    
    def stop(self):
        """Stop scheduler"""
        if not self.running:
            logger.warning("Scheduler is not running")
            return
        
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("Scheduler stopped")
    
    def _run(self):
        """Run scheduler loop"""
        while self.running:
            try:
                schedule.run_pending()
            except Exception as e:
                logger.error(f"Error in scheduler: {e}")
            time.sleep(1)
    
    def get_tasks(self):
        """
        Get list of registered tasks
        
        Returns:
            List of task dictionaries
        """
        return self.tasks.copy()
