#include "bb_sm.h"

#include <vector>

#include "json.h"


StateMachine::StateMachine(){

}

int StateMachine::randomPositionForTesting(){
    std::random_device dev;
    std::mt19937 rng(dev());
    int nPositions = possiblePositions.size();
    std::uniform_int_distribution<std::mt19937::result_type> dist6(0,nPositions -1); // distribution in range [1, 6]
    int randomNumber = dist6(rng);
    int randomPosToUse = possiblePositions[randomNumber];
    return randomPosToUse;
}

bool StateMachine::parseFile(std::string _fileLocation){
    std::ifstream animations_file(_fileLocation.c_str(), std::ifstream::binary);
    Json::Value animations;
    Json::Reader reader;
    if(! reader.parse(animations_file, animations, false)){
        std::cout << "json no good" << std::endl;
        return false;
    }
    std::vector<StateMachine::State> _states;
    std::vector<StateMachine::State> _earlyExits;
    for (Json::Value::ArrayIndex i = 0; i != animations.size(); i++){
        State state;
        state.name = std::string(animations[i].get("name", "nan" ).asString());
        state.startTime = int(animations[i].get("startTime", "nan" ).asInt());  //-1
        state.endTime = int(animations[i].get("endTime", "nan" ).asInt());      //inaccuracies in frame numbers?
        state.transitionFromParent = int(animations[i].get("transitionFromParent", "nan" ).asInt());    //try-1
        state.parentAnimation = std::string(animations[i].get("parentAnimation", "nan" ).asString());
        state.parentState = std::string(animations[i].get("parentState", "nan" ).asString());
        state.position = int(animations[i].get("position", "nan" ).asInt());
        state.targetPosition = int(animations[i].get("targetPosition", "nan" ).asInt());
        state.isEarlyExit = bool(animations[i].get("isEarlyExit", "nan" ).asBool());
        state.isActive = bool(animations[i].get("isActive", "nan" ).asBool());

        if(state.isEarlyExit == false){
	    _states.push_back(state);
        }
        else{
	    _earlyExits.push_back(state);
        }


	std::cout <<
	    "State {" <<
	    "  name: '" << state.name << "'" <<
	    ", start: " << state.startTime <<
	    ", end: " << state.endTime <<
	    ", early_exit: " << state.isEarlyExit <<
	    " }" <<
	    std::endl;

    }

    for(size_t s = 0; s < _states.size(); s ++){
        std::vector<StateMachine::State> _sortEarlyExits;
        std::string name = _states[s].name;
        for(size_t e = 0; e < _earlyExits.size(); e ++){
	    if(_earlyExits[e].parentAnimation == name){
		_sortEarlyExits.push_back(_earlyExits[e]);
	    }
        }
	//        std::sort(_earlyExits.begin(), _earlyExits.end());    //sort early exits on time from start of clip
	std::sort(_earlyExits.begin(), _earlyExits.end(),[](const State &a, const State &b) {
	    return a.transitionFromParent < b.transitionFromParent;
	});

        for(size_t e = 0; e < _sortEarlyExits.size(); e ++){
	    std::cout << "adding: " << _sortEarlyExits[e].name << "to: " << _states[s].name << std::endl;
	    _states[s].earlyExits.push_back(_sortEarlyExits[e]);
        }
        if(_states[s].earlyExits.size()>0){
	    _states[s].hasEarlyExits = true;
        }
    }

    for(size_t e = 0; e < _states.size(); e ++){
        states.push_back(_states[e]);
    }


    possiblePositions.push_back(states[0].position);
    for(size_t e = 0; e < states.size(); e ++){
        bool isUnique = true;
        for(size_t j = 0; j < possiblePositions.size(); j ++){
	    if(states[e].position == possiblePositions[j]){
		isUnique = false;
	    }
        }
        if(isUnique){
	    possiblePositions.push_back(states[e].position);
        }
    }
    for(size_t j = 0; j < possiblePositions.size(); j ++){
        std::cout << "possiblePositions: " << possiblePositions[j] << std::endl;
    }


    for(auto state : states){
        if(state.position == 2){
	    regrowths.push_back(state);
        }
    }
    return true;
}

void StateMachine::setTargetPosition(int position){
    //      position = randomNumberForTesting();

    //    targetPosition = possiblePositions[position % possiblePositions.size()];  //fix this, currently there are 2 positions for flowers: 3 for animals
    targetPosition = position;
    if(currentState.earlyExits.size() > size_t(0)){         //Has early exits in queue and wants to change state
	if(targetPosition != currentState.position){
	    if(targetPosition == 1){
		if(tempEarlyExits.size() > size_t(0)){
		    std::cout << "QueuingBigBloom: " << tempEarlyExits[0].name << std::endl;
		}
		else {
		    std::cout << "QueuingBigBloom: " << currentState.earlyExits[currentState.earlyExits.size()-1].name << std::endl;
		}

		lastTargetPosition = targetPosition;
		isExitingEarly = true;
	    }
	}
    }
}


void StateMachine::setIsActive(bool _isActive){
    isActive = _isActive;
}

StateMachine::State StateMachine::updateState(){

    earlyExitCount = 0;
    State _nextState = currentState; //needs had state or dequeue shuffler

    std::random_device rd;
    auto rng = std::default_random_engine { rd() };
    std::shuffle(std::begin(states), std::end(states), rng);    //random shuffle and dont get last

    //
    for(size_t i = 0; i < states.size(); i ++){
        State state = states[i];
        if(state.position == currentState.targetPosition){
	    if(state.name != currentState.name){            //State to state not interactive
		if(state.position == 2){                //regrowth state
		    if(regrowths.size() > 0){
			state = regrowths[regrowthN % regrowths.size()];
			regrowthN ++;
			regrowthN %= regrowths.size();
		    }
		}
		_nextState = state;
		return state;
	    }
        }
    }

    return _nextState;
}

void StateMachine::getTempEarlyExits(std::vector<State> _earlyExits){
    tempEarlyExits.clear();
    for(size_t i = 0; i < _earlyExits.size(); i ++){
	tempEarlyExits.push_back(_earlyExits[i]);
	lastEarlyExit = _earlyExits[i];
    }
}

bool StateMachine::getIsInit(){
    return init;
}

void StateMachine::updateSegment(){
    StateMachine::Segment _segment;

    if(init){
	currentState = states[0];
	//              currentState = updateState();
	getTempEarlyExits(currentState.earlyExits);

	lastTargetPosition = currentState.position;
	targetPosition = currentState.position;
	std::cout <<  "init2:" << currentState.name << "| EarlyExits0: " << tempEarlyExits.size() << std::endl;
	_segment.startTime = currentState.startTime;
	_segment.endTime = currentState.endTime;
	_segment.name = currentState.name;

	if(tempEarlyExits.size() > size_t(0)){  //has early exits in selected clip
	    _segment.startTime = currentState.startTime;
	    _segment.endTime = tempEarlyExits[0].transitionFromParent;
	    _segment.name = currentState.name;
	    std::cout << currentState.name << "| EarlyExitsInThisClip: " << tempEarlyExits.size() << std::endl;
	    //                      tempEarlyExits.pop_front();             //without creates offset
	}

	isExitingEarly = false;
	std::cout <<  "init :" << _segment.startTime << "end:" << _segment.endTime << std::endl;
	currentSegment = _segment;
	//              init = false;
	return;
	//              return _segment;
    }

    if (isExitingEarly) {
	if(tempEarlyExits.size() > size_t(0)){
	    currentState = tempEarlyExits[0];
	}
	else {
	    currentState = currentState.earlyExits[currentState.earlyExits.size()-1];
	}
	getTempEarlyExits(currentState.earlyExits);
	std::cout <<  "NewState:" << currentState.name << "| EarlyExits0: " << tempEarlyExits.size() << std::endl;
	_segment.startTime = currentState.startTime;
	_segment.endTime = currentState.endTime;
	_segment.name = currentState.name;
	isExitingEarly = false;
	std::cout <<  "start:" << _segment.startTime << "end:" << _segment.endTime << std::endl;
	currentSegment = _segment;
	currentPosition = currentState.targetPosition;
	return;
	//              return _segment;
    }

    if(tempEarlyExits.size() == size_t(0)){ //end of cycles, or no earlyExits in current clip
	currentState = updateState();
	getTempEarlyExits(currentState.earlyExits);
	std::cout << std::endl;
	std::cout <<  "NewState:" << currentState.name << "| EarlyExits1: " << tempEarlyExits.size() << std::endl;
	_segment.startTime = currentState.startTime;
	_segment.endTime = currentState.endTime;
	_segment.name = currentState.name;

	if(tempEarlyExits.size() > size_t(0)){  //has early exits in next clip
	    _segment.startTime = currentState.startTime;
	    _segment.endTime = tempEarlyExits[0].transitionFromParent;
	    _segment.name = currentState.name;
	    std::cout << currentState.name << "| EarlyExitsInThisClip: " << tempEarlyExits.size() << std::endl;
	    //                      tempEarlyExits.pop_front();             //without creates offset
	}
    }
    else{
	_segment.startTime = tempEarlyExits[0].transitionFromParent-1;// Start From the last Early Exit Transition Time
	tempEarlyExits.pop_front();
	std::cout << currentState.name << "| EarlyExitsLeft: " << tempEarlyExits.size() << std::endl ;


	if(tempEarlyExits.size() > size_t(0)){          //playToEndOnLastCycle  This works in the case that the last 'early exit' transition isn't the end of the clip
	    _segment.endTime = tempEarlyExits[0].transitionFromParent;      //-1 not work
	}
	else{
	    _segment.endTime = currentState.endTime;
	    std::cout <<  "Playing:" << currentState.name << " to end" << std::endl;
	}

	if(tempEarlyExits.size() > 0){
	    _segment.name = tempEarlyExits[0].parentAnimation;
	}
	else{
	    _segment.name = currentState.name;
	}
	//              tempEarlyExits.pop_front();
    }
    //      //This is because early exits now have early exits to the end ideally REFACTOR
    //      if((_segment.endTime == _segment.startTime) || (_segment.startTime > _segment.endTime)){
    //              currentState = updateState();
    //              getTempEarlyExits(currentState.earlyExits);
    //              _segment.startTime = currentState.startTime;
    //              _segment.endTime = currentState.endTime;
    //              _segment.name = currentState.name;
    //      }
    currentSegment = _segment;
    std::cout <<  "start:" << _segment.startTime << "end:" << _segment.endTime << std::endl;
    return;
    //      return _segment;
}



/* Input is frame */
bool StateMachine::updateFrame(int){
    return true;
}
