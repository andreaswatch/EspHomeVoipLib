#include "voip.h"
#include "automation.h"

namespace esphome {
namespace voip {

RingingTrigger::RingingTrigger(Voip *parent) {
  if (parent) parent->add_on_ringing_callback([this]() { this->trigger(); });
}

CallEstablishedTrigger::CallEstablishedTrigger(Voip *parent) {
  if (parent) parent->add_on_call_established_callback([this]() { this->trigger(); });
}

CallEndedTrigger::CallEndedTrigger(Voip *parent) {
  if (parent) parent->add_on_call_ended_callback([this]() { this->trigger(); });
}

ReadyTrigger::ReadyTrigger(Voip *parent) {
  if (parent) parent->add_on_ready_callback([this]() { this->trigger(); });
}

NotReadyTrigger::NotReadyTrigger(Voip *parent) {
  if (parent) parent->add_on_not_ready_callback([this]() { this->trigger(); });
}

}  // namespace voip
}  // namespace esphome
