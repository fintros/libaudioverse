/**Copyright (C) Austin Hicks, 2014
This file is part of Libaudioverse, a library for 3D and environmental audio simulation, and is released under the terms of the Gnu General Public License Version 3 or (at your option) any later version.
A copy of the GPL, as well as other important copyright and licensing information, may be found in the file 'LICENSE' in the root of the Libaudioverse repository.  Should this file be missing or unavailable to you, see <http://www.gnu.org/licenses/>.*/
#include <libaudioverse/libaudioverse.h>
#include <libaudioverse/libaudioverse_properties.h>
#include <libaudioverse/private/simulation.hpp>
#include <libaudioverse/private/resampler.hpp>
#include <libaudioverse/private/node.hpp>
#include <libaudioverse/private/properties.hpp>
#include <libaudioverse/private/macros.hpp>
#include <libaudioverse/private/memory.hpp>
#include <libaudioverse/private/kernels.hpp>
#include <limits>
#include <memory>
#include <algorithm>
#include <utility>
#include <vector>
#include <lambdatask/threadsafe_queue.hpp>

class LavPullNode: public LavNode {
	public:
	LavPullNode(std::shared_ptr<LavSimulation> sim, unsigned int inputSr, unsigned int channels);
	~LavPullNode();
	void process();
	unsigned int input_sr = 0, channels = 0;
	std::shared_ptr<LavResampler> resampler = nullptr;
	float* incoming_buffer = nullptr, *resampled_buffer = nullptr;
	LavPullNodeAudioCallback callback = nullptr;
	void* callback_userdata = nullptr;
};

LavPullNode::LavPullNode(std::shared_ptr<LavSimulation> sim, unsigned int inputSr, unsigned int channels): LavNode(Lav_NODETYPE_PULL, sim, 0, channels) {
	this->channels = channels;
	input_sr = inputSr;
	resampler = std::make_shared<LavResampler>(sim->getBlockSize(), channels, inputSr, (int)sim->getSr());
	this->channels = channels;
	incoming_buffer = LavAllocFloatArray(channels*simulation->getBlockSize());
	resampled_buffer = LavAllocFloatArray(channels*sim->getBlockSize());
	appendOutputConnection(0, channels);
}

std::shared_ptr<LavNode> createPullNode(std::shared_ptr<LavSimulation> simulation, unsigned int inputSr, unsigned int channels) {
	auto retval = std::shared_ptr<LavPullNode>(new LavPullNode(simulation, inputSr, channels), LavObjectDeleter(simulation));
	simulation->associateNode(retval);
	return retval;
}

LavPullNode::~LavPullNode() {
	LavFreeFloatArray(incoming_buffer);
	LavFreeFloatArray(resampled_buffer);
}

void LavPullNode::process() {
	//first get audio into the resampler if needed.
	int got = 0;
	while(got < block_size) {
		got += resampler->write(resampled_buffer, block_size-got);
		if(got >= block_size) break; //we may have done it on this iteration.
		if(callback) {
			callback(externalObjectHandle, block_size, channels, incoming_buffer, callback_userdata);
		} else {
			memset(incoming_buffer, 0, block_size*sizeof(float)*channels);
		}
		resampler->read(incoming_buffer);
	}
	//this is simply uninterweaving, but taking advantage of the fact that we have a different output destination.
	for(unsigned int i = 0; i < block_size*channels; i+=channels) {
		for(unsigned int j = 0; j < channels; j++) {
			output_buffers[j][i/channels] = resampled_buffer[i+j];
		}
	}
}

//begin public api.

Lav_PUBLIC_FUNCTION LavError Lav_createPullNode(LavHandle simulationHandle, unsigned int sr, unsigned int channels, LavHandle* destination) {
	PUB_BEGIN
	auto simulation = incomingObject<LavSimulation>(simulationHandle);
	LOCK(*simulation);
	*destination = outgoingObject<LavNode>(createPullNode(simulation, sr, channels));
	PUB_END
}

Lav_PUBLIC_FUNCTION LavError Lav_pullNodeSetAudioCallback(LavHandle nodeHandle, LavPullNodeAudioCallback callback, void* userdata) {
	PUB_BEGIN
	auto node = incomingObject<LavNode>(nodeHandle);
	LOCK(*node);
	if(node->getType() != Lav_NODETYPE_PULL) throw LavErrorException(Lav_ERROR_TYPE_MISMATCH);
	auto p = std::static_pointer_cast<LavPullNode>(node);
	p->callback = callback;
	p->callback_userdata = userdata;
	PUB_END
}
