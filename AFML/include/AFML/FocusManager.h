/*
 * FocusManager.h
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef ALEXA_CLIENT_SDK_AFML_INCLUDE_AFML_FOCUS_MANAGER_H_
#define ALEXA_CLIENT_SDK_AFML_INCLUDE_AFML_FOCUS_MANAGER_H_

#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include <AVSCommon/SDKInterfaces/ChannelObserverInterface.h>
#include <AVSCommon/SDKInterfaces/FocusManagerInterface.h>

#include "AFML/Channel.h"
#include "AVSCommon/Utils/Threading/Executor.h"

namespace alexaClientSDK {
namespace afml {

/**
 * The FocusManager takes requests to acquire and release Channels and updates the focuses of other Channels based on
 * their priorities so that the invariant that there can only be one Foreground Channel is held. The following 
 * operations are provided:
 *
 * acquire Channel - clients should call the acquireChannel() method, passing in the name of the Channel they wish to 
 * acquire, a pointer to the observer that they want to be notified once they get focus, and a unique activity id. 
 *
 * release Channel - clients should call the releaseChannel() method, passing in the name of the Channel and the
 * observer of the Channel they wish to release.
 *
 * stop foreground Channel - clients should call the stopForegroundActivitiy() method.
 *
 * All of these methods will notify the observer of the Channel of focus changes via an asynchronous callback to the 
 * ChannelObserverInterface##onFocusChanged() method, at which point the client should make a user observable change
 * based on the focus it receives.
 */
class FocusManager : public avsCommon::sdkInterfaces::FocusManagerInterface {
public:
    /**
     * The configuration used by the FocusManager to create Channel objects. Each configuration object has a 
     * name and priority.
     */
    struct ChannelConfiguration {
        /**
         * Constructor.
         *
         * @param configName The name of the Channel.
         * @param configPriority The priority of the channel. Lower number priorities result in higher priority
         * Channels. The highest priority number possible is 0.
         */
        ChannelConfiguration(const std::string & configName, unsigned int configPriority) 
            : name{configName}, priority{configPriority} {
        }

        /**
         * Converts the config to a string.
         *
         * @return A string version of the @c ChannelConfiguration data.
         */
        std::string toString() const {
            return "name:'" + name + "', priority:" + std::to_string(priority);
        }

        /// The name of the channel.
        std::string name;

        /// The priority of the channel.
        unsigned int priority;
    };

    /**
     * This constructor creates Channels based on the provided configurations. This is defaulted to the default
     * AVS Channel configuration names and priorities if no input parameter is provided.
     *
     * @param channelConfigurations A vector of @c channelConfiguration objects that will be used to create the 
     * Channels. No two Channels should have the same name or priority. If there are multiple configurations with the
     * same name or priority, the latter Channels with that name or priority will not be created.
     */
    FocusManager(const std::vector<ChannelConfiguration>& channelConfigurations = {
            {DIALOG_CHANNEL_NAME, DIALOG_CHANNEL_PRIORITY}, 
            {ALERTS_CHANNEL_NAME, ALERTS_CHANNEL_PRIORITY}, 
            {CONTENT_CHANNEL_NAME, CONTENT_CHANNEL_PRIORITY}
        });

    bool acquireChannel(
            const std::string& channelName, 
            std::shared_ptr<avsCommon::sdkInterfaces::ChannelObserverInterface> channelObserver,
            const std::string& activityId) override;

    std::future<bool> releaseChannel(
        const std::string& channelName,
        std::shared_ptr<avsCommon::sdkInterfaces::ChannelObserverInterface> channelObserver) override;

    void stopForegroundActivity() override;

private:
    /**
     * Functor so that we can compare Channel objects via shared_ptr.
     */
    struct ChannelPtrComparator {
        /**
         * Compares two shared_ptrs of Channel objects.
         *
         * @param first The first Channel to compare.
         * @param second The second Channel to compare.
         *
         * @return Returns @c true if the first Channel has a higher Channel priority than the second Channel.
         */
        bool operator()(const std::shared_ptr<Channel>& first, const std::shared_ptr<Channel>& second) const {
            return *first > *second;
        }
    };

    /**
     * Grants access to the Channel specified and updates other Channels as needed. This function provides the full 
     * implementation which the public method will call.
     *
     * @param channelToAcquire The Channel to acquire.
     * @param channelObserver The new observer of the Channel.
     * @param activityId The id of the new activity on the Channel.
     */
    void acquireChannelHelper(
            std::shared_ptr<Channel> channelToAcquire, 
            std::shared_ptr<avsCommon::sdkInterfaces::ChannelObserverInterface> channelObserver,
            const std::string& activityId);

    /**
     * Releases the Channel specified and updates other Channels as needed. This function provides the full 
     * implementation which the public method will call.
     *
     * @param channelToRelease The Channel to release.
     * @param channelObserver The observer of the Channel to release.
     * @param releaseChannelSuccess The promise to satisfy.
     * @param channelName The name of the Channel.
     */
    void releaseChannelHelper(
            std::shared_ptr<Channel> channelToRelease, 
            std::shared_ptr<avsCommon::sdkInterfaces::ChannelObserverInterface> channelObserver,
            std::shared_ptr<std::promise<bool>> releaseChannelSuccess,
            const std::string& channelName);

    /**
     * Stops the Channel specified and updates other Channels as needed if the activity id passed in matches the
     * activity id of the Channel. This function provides the full implementation which the public method will call.
     *
     * @param foregroundChannel The Channel to stop.
     * @param foregroundChannelActivityId The id of the activity to stop.
     */
    void stopForegroundActivityHelper(
        std::shared_ptr<Channel> foregroundChannel, std::string foregroundChannelActivityId);

    /**
     * Finds the channel from the given channel name.
     *
     * @param channelName The name of the channel to find.
     * @return Return a @c Channel if found or @c nullptr otherwise.
     */
    std::shared_ptr<Channel> getChannel(const std::string& channelName) const;

    /**
     * Gets the currently foregrounded Channel.
     *
     * @return Returns the highest priority active Channel if there is one and @c nullptr otherwise.
     */
    std::shared_ptr<Channel> getHighestPriorityActiveChannelLocked() const;

    /**
     * Checks to see if the provided channel currently has foreground focus or not.
     *
     * @param channel The channel to check.
     * @return Returns @c true if the @c Channel is foreground focused and @c false if it is not.
     */
    bool isChannelForegroundedLocked(const std::shared_ptr<Channel>& channel) const;

    /**
     * Checks to see if the provided Channel name already exists.
     *
     * @param name The Channel name to check.
     *
     * @return Returns @c true if the name is already associated with a Channel and @c false otherwise.
     */
    bool doesChannelNameExist(const std::string& name) const;

    /**
     * Checks to see if the provided Channel priority already exists.
     *
     * @param priority The Channel priority to check.
     *
     * @return Returns @c true if the priority is already associated with a Channel and @c false otherwise.
     */
    bool doesChannelPriorityExist(const unsigned int priority) const;

    /**
     * Foregrounds the highest priority active Channel.
     */
    void foregroundHighestPriorityActiveChannel();

    /// Map of channel names to shared_ptrs of Channel objects and contains every channel.
    std::unordered_map<std::string, std::shared_ptr<Channel>> m_allChannels;

    /// Set of currently observed Channels ordered by Channel priority.
    std::set<std::shared_ptr<Channel>, ChannelPtrComparator> m_activeChannels;

    /// An internal executor that performs execution of callable objects passed to it sequentially but asynchronously.
    avsCommon::utils::threading::Executor m_executor;

    /// Mutex used to lock m_activeChannels and Channels' activity ids.
    std::mutex m_mutex;
};

} // namespace afml
} // namespace alexaClientSDK

#endif // ALEXA_CLIENT_SDK_AFML_INCLUDE_AFML_FOCUS_MANAGER_H_
