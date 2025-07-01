#include "sm.h"

#include "spdlog/spdlog.h"


Bird::Bird(std::string file_path) {
    int curr_addr[2];
    clips = parse_spec(file_path.c_str(), &curr_addr, &max_clips);
    curr = get_clip(clips, &curr_addr);

}

Clip Bird::next() {
    int jind = rand() % curr.njumps;
    int addr[2];
    addr[0] = curr.addresses[jind * 2];
    addr[1] = curr.addresses[jind * 2 + 1];
    clip_t next = get_clip(clips, &addr);

    printf("Transition: (s%d.%d %d %d => s%d.%d %d %d)\n", curr.address[0],
	   curr.address[1], curr.start, curr.end, next.address[0],
	   next.address[1], next.start, next.end);

    curr = next;

    Clip clip;
    clip.start = curr.start;
    clip.end = curr.end;
    clip.name = std::to_string(curr.address[0]) + "." + std::to_string(curr.address[1]);
    return clip;
}

Clip Bird::current() {
    Clip clip;
    clip.start = curr.start;
    clip.end = curr.end;
    clip.name = std::to_string(curr.address[0]) + "." + std::to_string(curr.address[1]);
    return clip;
}

Clip Bird::seek(const std::string& clip_name) {
    spdlog::warn("Bird files do not support this feature yet.");
    return current();
}


Bird::~Bird() {
    if (clips) {
	for (int i = 0; i < max_clips; i++) {
	    if (clips[i]->addresses) {
		free(clips[i]->addresses);
		clips[i]->addresses = nullptr;
	    }
	}

	free(clips);
	clips = nullptr;
    }
}


BigBloom::BigBloom(std::string file_path) {
    sensor_manager.start();
    sm.parseFile(file_path);
    sm.updateSegment();
    sm.init = false;
}

Clip BigBloom::next() {
    sm.setTargetPosition(sensor_manager.data.active);
    sm.updateSegment();
    return current();
}

Clip BigBloom::current() {
    Clip clip;
    clip.start = sm.currentSegment.startTime;
    clip.end = sm.currentSegment.endTime;
    clip.name = sm.currentSegment.name;
    return clip;
}

Clip BigBloom::seek(const std::string& clip_name) {

    std::vector<StateMachine::State> all_states;

     for (auto state: sm.states) {
	 bool collected = false;
	 for (auto&& collected_state: all_states) {
	     collected = state.name == collected_state.name;
	     if (collected) {
		 break;
	     }
	 }

	 if (!collected) {
	     all_states.push_back(state);
	 }

	 for (auto ee_state: state.earlyExits) {
	     bool ee_collected = false;
	     for (auto&& collected_state: all_states) {
		 ee_collected = ee_state.name == collected_state.name;
		 if (ee_collected) {
		     break;
		 }
	     }

	     if (!ee_collected) {
		 all_states.push_back(ee_state);
	     }
	 }
     }

     for (auto&& state: all_states) {
	 if (state.name == clip_name) {
	    spdlog::info("Found clip '{}'", state.name);
	    sm.lastTargetPosition = sm.currentState.position;
	    sm.targetPosition = sm.currentState.position;

	    StateMachine::Segment target_segment;

	    sm.getTempEarlyExits(state.earlyExits);
	    target_segment.startTime = state.startTime;

	    if (sm.tempEarlyExits.size() > 0) {
		target_segment.endTime = sm.tempEarlyExits[0].transitionFromParent;
	    } else {
		target_segment.endTime = state.endTime;
	    }

	    sm.currentState = state;
	    sm.currentSegment = target_segment;
	    sm.init = false;

	    return current();
	}
    }

    spdlog::warn("Could not find clip: {}.", clip_name);

    return current();
}


BigBloom::~BigBloom() {

}
