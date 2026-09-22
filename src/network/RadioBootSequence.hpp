// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <stdint.h>
namespace rtl8852be { namespace network { namespace radioboot {
enum class Step {rck,dack,initialRxDc,channel,rxDc,iqk,tssi,dpk,scanBegin,scanTune,scanEnd};
enum class Action {initialize,tune,scanBegin,scanTune,scanEnd};
enum class Progress {pending,complete,failed};
enum class Stage {idle,upload,btInitialization,acquire,run,restore,ready,fault,stopped};
enum class Error {none,ownership,clock,timeout,backend,cancelled};
struct Result {Stage stage{Stage::idle};Error error{};Step step{};uint64_t completion{};
    bool initialized{},tuned{},fullCalibration{},scanning{},requiresRecovery{};};
// Backend owns real firmware/IO state. A pending ACK never advances a step.
// All calls run on one gate; each service runs at most one calibration body.
template<class Backend> class Sequence {
    Backend &io_;uint64_t started_{},last_{};bool active_{};Action action_{};unsigned index_{};
    bool fail(Error e){if(result.error==Error::none)result.error=e;result.stage=Stage::fault;
        result.tuned=result.fullCalibration=false;result.requiresRecovery=true;io_.failed();return false;}
    bool check(uint64_t now){
        if(!io_.inGate())return fail(Error::ownership);
        if(now<last_)return fail(Error::clock);last_=now;
        if(result.stage==Stage::fault||result.stage==Stage::stopped)return false;
        if(busy()&&now-started_>=((action_==Action::initialize||action_==Action::tune)?10000000U:2000000U))return fail(Error::timeout);
        return io_.healthy()||fail(Error::backend);
    }
    struct Guard {bool &v;~Guard(){v=false;}};
    bool acquire(){
        if(action_==Action::initialize)result.step=Step(index_);
        else if(action_==Action::tune)result.step=Step(unsigned(Step::channel)+index_);
        else if(action_==Action::scanTune)result.step=index_?Step::scanTune:Step::channel;
        else result.step=action_==Action::scanBegin?Step::scanBegin:Step::scanEnd;
        if(!io_.requestLease(result.step)||result.error!=Error::none)return fail(Error::backend);
        result.stage=Stage::acquire;return true;
    }
public:
    Result result{};explicit Sequence(Backend &io):io_(io){}
    bool busy()const{return result.stage!=Stage::idle&&result.stage!=Stage::ready&&result.stage!=Stage::fault&&result.stage!=Stage::stopped;}
    bool begin(uint64_t now){
        if(result.stage!=Stage::idle||active_)return false;
        if(!check(now))return false;active_=true;Guard guard{active_};started_=now;
        if(!io_.pause()||!io_.prepare()||result.error!=Error::none)return fail(Error::backend);
        result.stage=Stage::upload;return true;
    }
    Action action()const{return action_;}
    bool launch(Action action,uint64_t now){
        if(result.stage!=Stage::ready||!result.initialized||active_)return false;
        if(action==Action::initialize||(action==Action::tune&&result.scanning)||
           (action==Action::scanBegin&&(!result.fullCalibration||result.scanning))||
           ((action==Action::scanTune||action==Action::scanEnd)&&!result.scanning))return false;
        if(!check(now))return false;active_=true;Guard guard{active_};started_=now;
        action_=action;index_=0;result.tuned=result.fullCalibration=false;
        if(!io_.pause()||result.error!=Error::none)return fail(Error::backend);return acquire();
    }
    bool service(uint64_t now){
        if(active_)return fail(Error::ownership);
        if(!check(now))return false;active_=true;Guard guard{active_};
        Progress p;
        switch(result.stage){
        case Stage::upload:
            p=io_.upload();if(p==Progress::failed)return fail(Error::backend);
            if(p==Progress::complete){if(!io_.startBt())return fail(Error::backend);result.stage=Stage::btInitialization;}break;
        case Stage::btInitialization:
            p=io_.pollBt();if(p==Progress::failed)return fail(Error::backend);
            if(p==Progress::complete){index_=0;if(!acquire())return false;}break;
        case Stage::acquire:
            p=io_.pollLease();if(p==Progress::failed)return fail(Error::backend);
            if(p==Progress::complete)result.stage=Stage::run;break;
        case Stage::run:
            if(!io_.run(result.step)||!io_.finishLease())return fail(Error::backend);
            result.stage=Stage::restore;break;
        case Stage::restore:
            p=io_.pollRestore();if(p==Progress::failed)return fail(Error::backend);
            if(p==Progress::complete){
                const auto count=action_==Action::initialize?3U:action_==Action::tune?5U:action_==Action::scanTune?2U:1U;
                if(++index_<count){if(!acquire())return false;}
                else {if(!io_.finishOperation(action_))return fail(Error::backend);
                    result.initialized=true;result.tuned=action_!=Action::initialize;
                    result.fullCalibration=action_==Action::tune||action_==Action::scanEnd;
                    if(action_==Action::scanBegin)result.scanning=true;
                    if(action_==Action::scanEnd)result.scanning=false;
                    result.stage=Stage::ready;++result.completion;}
            }break;
        case Stage::idle:case Stage::ready:break;
        default:return false;
        }
        if(result.error!=Error::none){result.stage=Stage::fault;return false;}return true;
    }
    bool stop(){if(active_)return fail(Error::ownership);io_.failed();result.tuned=result.fullCalibration=false;result.stage=Stage::stopped;return true;}
};
} } }
