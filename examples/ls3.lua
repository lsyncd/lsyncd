----
-- Example lsyncd configuration for syncing with an Amazon S3 bucket
--
-- This requires the official AWS CLI to be available, and that credentials
-- bet set up through some external method, such as environment variables,
-- IAM profiles or the AWS SDK configuration.
--
-- The AWS CLI sync exclude rules are not as powerful as the ones supported by
-- lsyncd. Hence, some of the do not translate perfectly. For example, '*'
-- (asterisk) matches slashes, while it does not in lsyncd. Hence it is a good
-- idea to only use exclude patterns for full directories, either by using a
-- trailing / (slash) or ** (double asterisk), as those will be correctly
-- translated.
--
-- An initialSync options is provided as a convenience, since it's not easy to
-- make sure exclusion rules match when doing it manually. It will *pull* from
-- the target bucket to the local dir (the opposite of the regular behavior)
-- then exit immediately.
--
-- Author: Daniel Miranda <danielkza2@gmail.com>
--
s3 = {}

s3.checkgauge = {
    onCreate  = false,
    onModify  = false,
    onDelete  = false,
    onStartup = false,
    onMove    = false,

    delete      = true,
    exclude     = true,
    excludeFrom = true,
    target      = true,

    s3 = {
        -- Path to the AWS CLI binary
        awscliBinary  = true,
        -- Extra options to pass to the AWS CLI (as a list)
        awscliOptions = true,
        -- Whether to do a dry-run, and not make any real changes
        dryrun        = true,
        -- Do an initial pull from the bucket and exit immediately.
        initialSync   = true
    }
}

-- Generate a list of exclude flags for the AWS CLI  based on the lsyncd
-- patterns provided. Cache it to avoid re-generating it every time.

local s3Excludes = function(config, excludes)
    if config.s3._excludes == nil then
        config.s3._excludes = {}
        for _, pat in ipairs(excludes) do
            pat = pat:gsub('%*%*', '[[ANY]]')
            pat = pat:gsub('%?',   '[[ANY_BUT_SLASH_ONCE]]')
            pat = pat:gsub('/$',   '/*')
            pat = pat:gsub('%[%[ANY%]%]',                '*')
            pat = pat:gsub('%[%[ANY_BUT_SLASH_ONCE%]%]', '[^/]')

            if pat:match('^/') then
                pat = pat:sub(2, -1)
            else
                pat = '*/' .. pat
            end

            table.insert(config.s3._excludes, '--exclude')
            table.insert(config.s3._excludes, pat)
        end

        log('s3Excludes', table.concat(config.s3._excludes, '\n'))
    end

    return config.s3._excludes
end

-- Generates a command line to call the AWS CLI as configured, with the provided
-- S3 action (such as cp, mv, rm or sync).
-- Returns a tuple of (binaryPath, arguments)
local awscliCommand = function(verb, config)
    local bin = config.s3.awscliBinary
    local args = {'s3', verb, '--only-show-errors'}
    if config.s3.dryrun then
        table.insert(args, '--dryrun')
    end

    if verb == 'sync'
       and (config.delete == true or config.delete == 'startup')
    then
        table.insert(args, '--delete')
    end

    for _, opt in ipairs(config.s3.awscliOptions) do
        table.insert(args, opt)
    end

    return bin, args
end

s3.action = function(inlet)
    local event, event2 = inlet.getEvent()
    -- S3 never actually deals with directories - they are just an illusion
    -- created based on the common prefixes of objects. Hence discard any events
    -- that do not concern files.
    if event.isdir then
        inlet.discardEvent(event)
        return
    end

    local config = inlet.getConfig()
    if event.etype == 'Create' or event.etype == 'Modify' then
        local bin, args = awscliCommand('cp', config)
        spawn(
            event,
            bin,
            args,
            event.sourcePath,
            event.targetPath
        )
    elseif event.etype == 'Delete' then
        if config.delete ~= true and config.delete ~= 'running' then
            inlet.discardEvent(event)
            return
        end

        local bin, args = awscliCommand('rm', config)
        spawn(
            event,
            bin,
            args,
            event.targetPath
        )
    elseif event.etype == 'Move' then
        local bin, args = awscliCommand('mv', config)
        spawn(
            event,
            bin,
            args,
            event.targetPath,
            event2.targetPath
        )
    else
        log('Warn', 'ignored an event of type "', event.etype, '"')
        inlet.discardEvent(event)
    end
end

s3.init = function(event)
    local config = event.config
    local inlet = event.inlet
    local excludes = s3Excludes(config, inlet.getExcludes())
    local bin, args = awscliCommand('sync', config)

    -- Do a pull when initialSync is enabled.
    if config.s3.initialSync then
        spawn(
            event,
            bin,
            args,
            excludes,
            config.target,
            event.sourcePath
        )
    -- And a push, as usual, otherwise
    else
        spawn(
            event,
            bin,
            args,
            excludes,
            event.sourcePath,
            config.target
        )
    end
end

-- Define a collect callback so we can terminate immediately when initialSync
-- is enabled
s3.collect = function(agent, exitcode)
    local config = agent.config
    if not agent.isList and agent.etype == 'Init' and config.s3.initialSync then
        terminate(exitcode == 0 and 0 or -1)
    end

    return
end

s3.prepare = function(config, level)
    default.prepare(config, level + 1)

    config.target = config.target:gsub('/+$', '')
    if not config.target:match('^s3://') then
        config.target = 's3://' .. config.target
    end
end

s3.s3 = {
    awscliBinary  = '/usr/bin/aws',
    awscliOptions = {},
    dryrun       = false,
    initialSync  = false
}
s3.delete       = false
s3.delay        = 10
s3.maxProcesses = 1

sync {
    s3,
    source       = '/my/dir',
    target       = 's3://my-bucket/my-path',
    delay        = 30,
    delete       = true,
    maxProcesses = 2,
    exclude = {
        '/sub/folder/',
    },
    s3 = {
        awscliBinary  = '/usr/local/bin/aws',
        awscliOptions = {'--acl', 'public-read'},
        dryrun        = false
    }
}
