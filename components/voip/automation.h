#pragma once

#include "esphome/core/automation.h"
#include "voip.h"

// In case of include order issues, forward-declare Voip to guarantee availability
namespace esphome { namespace voip { class Voip; } }

namespace esphome {
namespace voip {

class RingingTrigger : public Trigger<> {
 public:
  explicit RingingTrigger(Voip *parent);
};

class CallEstablishedTrigger : public Trigger<> {
 public:
  explicit CallEstablishedTrigger(Voip *parent);
};

class CallEndedTrigger : public Trigger<> {
 public:
  explicit CallEndedTrigger(Voip *parent);
};

class ReadyTrigger : public Trigger<> {
 public:
  explicit ReadyTrigger(Voip *parent);
};

class NotReadyTrigger : public Trigger<> {
 public:
  explicit NotReadyTrigger(Voip *parent);
};

}  // namespace voip
}  // namespace esphome
