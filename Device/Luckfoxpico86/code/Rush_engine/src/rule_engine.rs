use glob::Pattern;
use log::{debug, error, info};
use serde::Deserialize;
use serde_json::Value;
use std::fs;
use tokio::sync::mpsc;


#[derive(Debug, Clone, Deserialize)]
pub struct RuleTrigger {
    #[serde(rename = "type")]
    pub trigger_type: String,
    pub topic: Option<String>,
    pub at: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct RuleCondition {
    #[serde(rename = "type")]
    pub condition_type: String,
    pub topic: Option<String>,
    pub equals: Option<String>,
    pub above: Option<f64>,
    pub below: Option<f64>,
    pub after: Option<String>,
    pub before: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct RuleAction {
    #[serde(rename = "type")]
    pub action_type: String,
    pub topic: Option<String>,
    pub payload: Option<Value>,
    pub qos: Option<u8>,
    pub retain: Option<bool>,
    pub message: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Rule {
    pub alias: Option<String>,
    #[serde(default = "default_enabled")]
    pub enabled: bool,
    #[serde(default)]
    pub triggers: Vec<RuleTrigger>,
    #[serde(default)]
    pub conditions: Vec<RuleCondition>,
    #[serde(default)]
    pub actions: Vec<RuleAction>,
}

fn default_enabled() -> bool {
    true
}

#[derive(Debug, Deserialize)]
struct RulesFile {
    #[serde(default)]
    rules: Vec<Rule>,
}

pub struct RuleEngine {
    rules: Vec<Rule>,
    rules_file: String,
    action_tx: Option<mpsc::UnboundedSender<RuleAction>>,
    last_fired_date: std::collections::HashMap<String, String>,
    /// mtime (secs) of the rules file at last successful load, for hot-reload.
    rules_mtime: u64,
}

impl RuleEngine {
    pub fn new(rules_file: &str) -> Self {
        let mut engine = Self {
            rules: Vec::new(),
            rules_file: rules_file.to_string(),
            action_tx: None,
            last_fired_date: std::collections::HashMap::new(),
            rules_mtime: 0,
        };
        engine.load_rules();
        engine
    }

    /// Attach the channel that receives fired actions. Set once at startup.
    pub fn set_action_sender(&mut self, tx: mpsc::UnboundedSender<RuleAction>) {
        self.action_tx = Some(tx);
    }

    fn dispatch_actions(&self, rule: &Rule) {
        for action in &rule.actions {
            if let Some(tx) = &self.action_tx {
                let _ = tx.send(action.clone());
            }
            info!(
                "Rule executed: {} -> action type: {}",
                rule.alias.as_deref().unwrap_or("unknown"),
                action.action_type
            );
        }
    }

    pub fn load_rules(&mut self) {
        match fs::read_to_string(&self.rules_file) {
            Ok(content) => match serde_yaml::from_str::<RulesFile>(&content) {
                Ok(data) => {
                    self.rules = data.rules;
                    self.rules_mtime = self.file_mtime();
                    info!("Loaded {} rules from {}", self.rules.len(), self.rules_file);
                }
                Err(e) => {
                    error!("Failed to parse rules from {}: {}", self.rules_file, e);
                    self.rules = Vec::new();
                }
            },
            Err(e) => {
                error!("Failed to load rules from {}: {}", self.rules_file, e);
                self.rules = Vec::new();
            }
        }
    }

    /// Timestamp (secs) of the rules file at last successful load.
    fn file_mtime(&self) -> u64 {
        std::fs::metadata(&self.rules_file)
            .ok()
            .and_then(|m| m.modified().ok())
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map(|d| d.as_secs())
            .unwrap_or(0)
    }

    /// Hot-reload: reload rules.yaml when the file has changed since last load.
    /// Called periodically (e.g. every 5s tick) before evaluating time rules.
    pub fn check_reload(&mut self) {
        if !std::path::Path::new(&self.rules_file).exists() {
            return;
        }
        let current = self.file_mtime();
        if current > self.rules_mtime {
            info!("Rules file changed, hot-reloading...");
            let old_count = self.rules.len();
            self.load_rules();
            info!("Hot-reloaded rules: {} -> {} rules", old_count, self.rules.len());
        }
    }

    pub fn process_message(&self, topic: &str, payload: &Value) {
        for rule in &self.rules {
            if !rule.enabled {
                continue;
            }

            let triggered = rule.triggers.iter().any(|trigger| self.evaluate_trigger(trigger, topic, payload));
            if !triggered {
                continue;
            }

            let conditions_met = rule.conditions.iter().all(|condition| self.evaluate_condition(condition));
            if !conditions_met {
                continue;
            }

            self.dispatch_actions(rule);
        }
    }

    pub fn evaluate_trigger(&self, trigger: &RuleTrigger, topic: &str, _payload: &Value) -> bool {
        match trigger.trigger_type.as_str() {
            "mqtt" => {
                if let Some(pattern) = &trigger.topic {
                    self.match_topic(pattern, topic)
                } else {
                    false
                }
            }
            "time" => false, // time triggers are evaluated by process_time_tick
            _ => false,
        }
    }

    /// Evaluate all time-triggered rules. Called on a fixed tick (e.g. 5s).
    /// A rule fires at most once per calendar day at/after its `at` time.
    pub fn process_time_tick(&mut self) {
        let now = chrono::Local::now();
        let now_time = now.time();
        let today = now.format("%Y-%m-%d").to_string();

        for rule in &self.rules {
            if !rule.enabled {
                continue;
            }

            let triggered = rule.triggers.iter().any(|trigger| {
                if trigger.trigger_type != "time" {
                    return false;
                }
                let Some(at) = &trigger.at else {
                    return false;
                };
                match chrono::NaiveTime::parse_from_str(at, "%H:%M:%S") {
                    Ok(at_time) => now_time >= at_time,
                    Err(_) => false,
                }
            });
            if !triggered {
                continue;
            }

            let key = rule.alias.clone().unwrap_or_else(|| "rule".to_string());
            if self.last_fired_date.get(&key) == Some(&today) {
                continue;
            }

            let conditions_met = rule.conditions.iter().all(|condition| self.evaluate_condition(condition));
            if !conditions_met {
                continue;
            }

            self.last_fired_date.insert(key, today.clone());
            self.dispatch_actions(rule);
        }
    }

    pub fn evaluate_condition(&self, condition: &RuleCondition) -> bool {
        match condition.condition_type.as_str() {
            "state" => self.check_state_condition(condition),
            "numeric_state" => self.check_numeric_state_condition(condition),
            "time" => self.check_time_condition(condition),
            _ => false,
        }
    }

    fn match_topic(&self, pattern: &str, topic: &str) -> bool {
        let pattern = pattern.replace('+', "*").replace('#', "**");
        Pattern::new(&pattern).map(|p| p.matches(topic)).unwrap_or(false)
    }

    fn check_state_condition(&self, condition: &RuleCondition) -> bool {
        let topic = condition.topic.as_deref().unwrap_or("");
        debug!(
            "State condition check: {} == {:?}",
            topic, condition.equals
        );
        true // Placeholder
    }

    fn check_numeric_state_condition(&self, condition: &RuleCondition) -> bool {
        let topic = condition.topic.as_deref().unwrap_or("");
        debug!(
            "Numeric state condition check: {} above={:?} below={:?}",
            topic, condition.above, condition.below
        );
        true // Placeholder
    }

    fn check_time_condition(&self, condition: &RuleCondition) -> bool {
        let now = chrono::Local::now().time();

        if let Some(after) = &condition.after {
            if let Ok(after_time) = chrono::NaiveTime::parse_from_str(after, "%H:%M:%S") {
                if now < after_time {
                    return false;
                }
            }
        }

        if let Some(before) = &condition.before {
            if let Ok(before_time) = chrono::NaiveTime::parse_from_str(before, "%H:%M:%S") {
                if now > before_time {
                    return false;
                }
            }
        }

        true
    }

    pub fn reload_rules(&mut self) {
        info!("Reloading rules...");
        self.load_rules();
    }
}
