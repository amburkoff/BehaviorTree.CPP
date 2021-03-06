/* Copyright (C) 2015-2018 Michele Colledanchise -  All Rights Reserved
 * Copyright (C) 2018 Davide Faconti -  All Rights Reserved
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
*   to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
*   and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
*   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "behaviortree_cpp/action_node.h"
#include "coroutine/coroutine.h"

namespace BT
{
ActionNodeBase::ActionNodeBase(const std::string& name, const NodeParameters& parameters)
  : LeafNode::LeafNode(name, parameters)
{
}

NodeStatus ActionNodeBase::executeTick()
{
    initializeOnce();
    NodeStatus prev_status = status();

    if (prev_status == NodeStatus::IDLE || prev_status == NodeStatus::RUNNING)
    {
        setStatus(tick());
    }
    return status();
}

//-------------------------------------------------------

SimpleActionNode::SimpleActionNode(const std::string& name,
                                   SimpleActionNode::TickFunctor tick_functor,
                                   const NodeParameters& params)
  : ActionNodeBase(name, params), tick_functor_(std::move(tick_functor))
{
}

NodeStatus SimpleActionNode::tick()
{
    NodeStatus prev_status = status();

    if (prev_status == NodeStatus::IDLE)
    {
        setStatus(NodeStatus::RUNNING);
        prev_status = NodeStatus::RUNNING;
    }

    NodeStatus status = tick_functor_(*this);
    if (status != prev_status)
    {
        setStatus(status);
    }
    return status;
}

//-------------------------------------------------------

AsyncActionNode::AsyncActionNode(const std::string& name, const NodeParameters& parameters)
  : ActionNodeBase(name, parameters), loop_(true)
{
    thread_ = std::thread(&AsyncActionNode::waitForTick, this);
}

AsyncActionNode::~AsyncActionNode()
{
    if (thread_.joinable())
    {
        stopAndJoinThread();
    }
}

void AsyncActionNode::waitForTick()
{
    while (loop_.load())
    {
        tick_engine_.wait();

        // check loop_ again because the tick_engine_ could be
        // notified from the method stopAndJoinThread
        if (loop_ && status() == NodeStatus::IDLE)
        {
            setStatus(NodeStatus::RUNNING);
            setStatus(tick());
        }
    }
}

NodeStatus AsyncActionNode::executeTick()
{
    initializeOnce();
    //send signal to other thread.
    // The other thread is in charge for changing the status
    if (status() == NodeStatus::IDLE)
    {
        tick_engine_.notify();
    }

    // block as long as the state is NodeStatus::IDLE
    const NodeStatus stat = waitValidStatus();
    return stat;
}

void AsyncActionNode::stopAndJoinThread()
{
    loop_.store(false);
    tick_engine_.notify();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

//-------------------------------------
struct CoroActionNode::Pimpl
{
    coroutine::routine_t coro;
    Pimpl(): coro(0) {}
};


CoroActionNode::CoroActionNode(const std::string &name,
                               const NodeParameters &parameters):
  ActionNodeBase (name, parameters),
  _p(new  Pimpl)
{
}

CoroActionNode::~CoroActionNode()
{
    halt();
}

void CoroActionNode::setStatusRunningAndYield()
{
    setStatus( NodeStatus::RUNNING );
    coroutine::yield();
}

NodeStatus CoroActionNode::executeTick()
{
    initializeOnce();
    if (status() == NodeStatus::IDLE)
    {
        _p->coro = coroutine::create( [this]() { setStatus(tick()); } );
    }

    if( _p->coro != 0)
    {
        auto res = coroutine::resume(_p->coro);

        if( res == coroutine::ResumeResult::FINISHED)
        {
            coroutine::destroy(_p->coro);
            _p->coro = 0;
        }
    }
    return status();
}

void CoroActionNode::halt()
{
    if( _p->coro != 0 )
    {
        coroutine::destroy(_p->coro);
        _p->coro = 0;
    }
}

SyncActionNode::SyncActionNode(const std::string &name, const NodeParameters &parameters):
    ActionNodeBase(name, parameters)
{}

NodeStatus SyncActionNode::executeTick()
{
    auto stat = ActionNodeBase::executeTick();
    if( stat == NodeStatus::RUNNING)
    {
        throw std::logic_error("SyncActionNode MUSt never return RUNNING");
    }
    return stat;
}



}
