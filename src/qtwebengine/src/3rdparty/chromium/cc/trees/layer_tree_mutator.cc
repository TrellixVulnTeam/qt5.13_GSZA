// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/layer_tree_mutator.h"

#include <algorithm>

namespace cc {

AnimationWorkletInput::AddAndUpdateState::AddAndUpdateState(
    WorkletAnimationId worklet_animation_id,
    std::string name,
    double current_time,
    std::unique_ptr<AnimationOptions> options)
    : worklet_animation_id(worklet_animation_id),
      name(name),
      current_time(current_time),
      options(std::move(options)) {}
AnimationWorkletInput::AddAndUpdateState::AddAndUpdateState(
    AddAndUpdateState&&) = default;
AnimationWorkletInput::AddAndUpdateState::~AddAndUpdateState() = default;

#if DCHECK_IS_ON()
bool AnimationWorkletInput::ValidateScope(int scope_id) const {
  return std::all_of(added_and_updated_animations.cbegin(),
                     added_and_updated_animations.cend(),
                     [scope_id](auto& it) {
                       return it.worklet_animation_id.scope_id == scope_id;
                     }) &&
         std::all_of(updated_animations.cbegin(), updated_animations.cend(),
                     [scope_id](auto& it) {
                       return it.worklet_animation_id.scope_id == scope_id;
                     }) &&
         std::all_of(
             removed_animations.cbegin(), removed_animations.cend(),
             [scope_id](auto& it) { return it.scope_id == scope_id; }) &&
         std::all_of(peeked_animations.cbegin(), peeked_animations.cend(),
                     [scope_id](auto& it) { return it.scope_id == scope_id; });
}
#endif

AnimationWorkletInput::AnimationWorkletInput() = default;
AnimationWorkletInput::~AnimationWorkletInput() = default;

MutatorInputState::MutatorInputState() = default;
MutatorInputState::~MutatorInputState() = default;

bool MutatorInputState::IsEmpty() const {
  // If there is an AnimationWorkletInput entry in the map then that entry is
  // guranteed to be non-empty. So checking |inputs_| map emptiness is
  // sufficient.
  return inputs_.empty();
}

AnimationWorkletInput& MutatorInputState::EnsureWorkletEntry(int id) {
  auto it = inputs_.find(id);
  if (it == inputs_.end())
    it = inputs_.emplace_hint(it, id, std::unique_ptr<AnimationWorkletInput>(new AnimationWorkletInput));

  return *it->second;
}

void MutatorInputState::Add(AnimationWorkletInput::AddAndUpdateState&& state) {
  AnimationWorkletInput& worklet_input =
      EnsureWorkletEntry(state.worklet_animation_id.scope_id);
  worklet_input.added_and_updated_animations.push_back(std::move(state));
}
void MutatorInputState::Update(AnimationWorkletInput::UpdateState&& state) {
  AnimationWorkletInput& worklet_input =
      EnsureWorkletEntry(state.worklet_animation_id.scope_id);
  worklet_input.updated_animations.push_back(std::move(state));
}
void MutatorInputState::Remove(WorkletAnimationId worklet_animation_id) {
  AnimationWorkletInput& worklet_input =
      EnsureWorkletEntry(worklet_animation_id.scope_id);
  worklet_input.removed_animations.push_back(worklet_animation_id);
}

void MutatorInputState::Peek(WorkletAnimationId worklet_animation_id) {
  AnimationWorkletInput& worklet_input =
      EnsureWorkletEntry(worklet_animation_id.scope_id);
  worklet_input.peeked_animations.push_back(worklet_animation_id);
}

std::unique_ptr<AnimationWorkletInput> MutatorInputState::TakeWorkletState(
    int scope_id) {
  auto it = inputs_.find(scope_id);
  if (it == inputs_.end())
    return nullptr;

  std::unique_ptr<AnimationWorkletInput> result = std::move(it->second);
  inputs_.erase(it);
  return result;
}

AnimationWorkletOutput::AnimationWorkletOutput() = default;
AnimationWorkletOutput::~AnimationWorkletOutput() = default;

AnimationWorkletOutput::AnimationState::AnimationState(
    WorkletAnimationId id,
    base::Optional<base::TimeDelta> time)
    : worklet_animation_id(id), local_time(time) {}
AnimationWorkletOutput::AnimationState::AnimationState(const AnimationState&) =
    default;

}  // namespace cc
