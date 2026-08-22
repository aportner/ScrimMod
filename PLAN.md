# **Scrim Mod Plan**

Scrim Mod is a Metamod-R plugin that adds functionality for automating competitive play. This includes organizing matches, picking teams, running knife rounds, drafting players, managing halves and overtime, and tracking the match through completion.

## **Implementation**

Scrim Mod MUST have a setting to enable or disable it.

If Scrim Mod is disabled, plugin hooks should immediately return without performing any Scrim Mod behavior. Its internal match state should be completely reset when disabled.

Disabling Scrim Mod should:

* Clear captains  
* Clear rosters  
* Clear scores  
* Clear draft state  
* Clear ready state  
* Clear knife-round state  
* Remove any player/team/weapon restrictions  
* Restore normal team selection behavior  
* Execute `pregame.cfg`

`pregame.cfg` should be considered authoritative for restoring the server to its non-scrim configuration.

## **Match State**

Scrim Mod should maintain its own authoritative match state.

The game server's CT/T assignments and scoreboard should be treated as a representation of Scrim Mod's state, not as the source of truth.

The match should consist of two persistent logical teams:

Team A  
Team B

Each logical team should contain:

Captain  
Players  
Total Match Score  
Current Period Score  
Current Side (CT/T)

Logical teams do not change when sides switch.

For example, if Team A wins 7 rounds as Terrorists during the first half, Team A's score remains 7 after halftime even though Team A is now CT.

Internally:

Team A: 7  
Team B: 5

should remain unchanged.

Only the mapping changes:

First half:  
Team A \-\> T  
Team B \-\> CT

Second half:  
Team A \-\> CT  
Team B \-\> T

The plugin may update CS's displayed CT/T scores as necessary, but internal score tracking should always be based on Team A and Team B.

## **Players**

Players should be tracked by Steam ID rather than player slot, entity index, or name.

Player slots and entity indexes are temporary and MUST NOT be used as persistent player identifiers.

A player should conceptually contain:

Steam ID  
Last Known Name  
Logical Team  
Connected/Disconnected State

If a player disconnects and reconnects, Scrim Mod should recognize the Steam ID and restore that player's match assignment.

For example:

Player belongs to Team A  
Team A is currently CT  
Player reconnects  
\-\> Player is placed on CT

Disconnecting should not remove a player from a drafted roster.

## **Eligible Player Pool**

When a scrim begins, Scrim Mod should capture a roster of eligible players.

Eligible players should generally be:

* Human players  
* Connected  
* Not HLTV  
* Not explicitly excluded

The captured roster should normally remain fixed for the match.

Players connecting after the roster is captured should not automatically become draft-eligible.

Admin commands should exist to explicitly add or remove players if necessary.

For example:

scrim\_add \<player\>  
scrim\_remove \<player\>

## **States**

Scrim Mod should operate as an explicit state machine.

Possible states include:

Disabled  
CaptainSelection  
KnifeSetup  
KnifeLive  
KnifeComplete  
SideOrPick  
Draft  
Ready  
RegulationFirstHalf  
Halftime  
RegulationSecondHalf  
OvertimeFirstHalf  
OvertimeHalftime  
OvertimeSecondHalf  
MatchComplete

Some of these may instead be represented as substates if that results in a cleaner implementation.

All state changes should go through a central state transition mechanism.

Conceptually:

SetState(newState)

Entering a state should reconcile the actual server with the expected state.

For example, entering `Draft` should ensure:

* Knife restrictions are removed  
* Captains are on their selected sides  
* Unpicked players are spectators  
* Eligible-player state is correct  
* Draft turn order is correct

The plugin should not rely on a collection of loosely related flags such as:

knife \= true  
drafting \= true  
live \= false

The state machine should be authoritative.

## **Rewinding State**

Scrim Mod should support rewinding the match to earlier major phases.

For example, if LO3 begins but someone crashes, an admin should be able to rewind to `Ready` and run the start sequence again.

Rewinding should restore the server to the expected condition for the target phase.

For example:

Live \-\> Ready

should:

* Stop score accumulation  
* Restore proper team assignments  
* Preserve captain selection  
* Preserve draft results  
* Preserve starting sides  
* Clear captain ready flags  
* Restore the server to pre-live settings

Rewinding farther:

Draft \-\> CaptainSelection

should invalidate everything dependent upon captain selection, including:

* Knife result  
* Side/pick result  
* Draft  
* Ready state  
* Match score

Rewinding should initially operate at phase/checkpoint granularity.

Scrim Mod does NOT need to support arbitrary rollback to an individual previously played round.

## **Picking Captains**

To start the match, an admin should select two captains.

The UI should list all eligible players and include an option to select a random eligible player.

Once one captain is selected, that player should no longer be eligible for the second captain selection.

HLTV and other ineligible clients must not appear.

After both captains are selected, the UI should display both selections and ask the admin to confirm them.

For example:

Captain 1: dook  
Captain 2: disgrace

Confirm?

Captains should be stored by Steam ID.

## **Knife Round**

Once captains are selected:

* All non-captains should be moved to spectator  
* Non-captains should be prevented from joining either team  
* Captains should be assigned opposite teams  
* Which captain starts CT/T should be randomized  
* Captains should only be allowed to use knives

Knife restrictions should be enforced throughout the knife round, including after spawning.

Weapon pickup should be prevented or any acquired weapon should be stripped.

The plugin should track the winner and loser of the knife round.

If one captain kills the other normally:

Killer \-\> Knife Winner  
Dead Captain \-\> Knife Loser

If the round ends ambiguously due to:

* Suicide  
* World damage  
* Disconnect  
* Unexpected round termination

the knife round should normally be replayed.

Admin recovery commands should also exist, such as:

scrim\_knife\_restart  
scrim\_knife\_winner \<player\>

## **Side or Pick**

The knife-round winner should choose between:

Choose Starting Side  
First Draft Pick

The choice should require confirmation.

For example:

You chose FIRST PICK.  
Are you sure?

If the knife winner chooses the starting side, the knife loser receives first draft pick.

If the knife winner chooses first draft pick, the knife loser chooses the starting side.

Side selection should also require confirmation:

You chose to start CT.  
Are you sure?

The result should explicitly store:

Knife Winner  
Knife Loser  
First Picker  
Team A Starting Side  
Team B Starting Side

These values should not need to be inferred later.

If necessary, captains should be switched to the correct CT/T teams after the side choice.

## **Draft**

The remaining players should then be drafted onto Team A and Team B.

Only eligible undrafted players should be shown.

The plugin should support two draft modes:

AB  
Snake

Each choice should require confirmation.

For example:

You chose efegege.  
Is this correct?

Once confirmed, the player should:

* Be assigned to the captain's logical team  
* Be moved to that team's current CT/T side if connected

### **AB Draft**

Captains alternate one player at a time:

A  
B  
A  
B  
A  
B  
...

### **Snake Draft**

The first captain receives one pick and captains then receive alternating pairs:

A  
B B  
A A  
B B  
A A  
...

The plugin should explicitly track:

Current Captain  
Number of Picks Remaining in Turn  
Available Players  
Drafted Players

rather than attempting to infer this from roster sizes.

## **Draft Disconnect Handling**

The draft must tolerate players disconnecting.

If an unpicked player disconnects, the player's Steam ID should remain in the eligible player pool and the UI should indicate that the player is disconnected.

For example:

garetjax  
foo  
bar (disconnected)

A disconnected player may still be drafted.

This prevents disconnect/reconnect timing from manipulating draft availability.

If a drafted player disconnects, the player remains on the drafted logical team.

If a captain disconnects during a captain-dependent stage, the process should pause until:

* The captain reconnects, or  
* An admin takes corrective action

## **Draft UI**

Menu entries should refer to stable internal player identifiers rather than temporary menu positions.

For example, if menu option 2 represented a particular Steam ID when the menu was created, option 2 should still resolve to that Steam ID even if another player disconnects before the selection callback occurs.

The UI should support pagination when necessary.

Chat/console commands should exist as a fallback when menus fail or disappear.

Examples:

.pick \<player\>  
.side ct  
.side t  
.ready  
.unready

## **Ready**

After drafting is complete, both captains must indicate that their teams are ready.

The primary protocol should be:

.ready  
.unready

A menu may also be presented as a convenience.

Internally:

Team A Captain Ready  
Team B Captain Ready

should be tracked separately.

When both captains are ready, Scrim Mod begins the match start sequence.

Any significant change to the match state prior to going live should clear ready status.

This includes:

* Roster changes  
* Team changes  
* Side changes  
* Rewinding to Ready

## **LO3**

Scrim Mod should first execute cal.cfg which contains all the settings for scrim mode. Then it should execute a standard live-on-three sequence:

sv\_restart 1  
sv\_restart 1  
sv\_restart 3

## **First Half**

Once LO3 completes, the match becomes live.

Scrim Mod should:

* Track every completed round  
* Attribute round wins to Team A or Team B  
* Maintain total match score  
* Maintain current-half score  
* Announce the score after every round

For example:

Team A 7 \- 5 Team B

The score should be based on logical teams rather than CT/T.

### **Last Round Warning**

Before the final guaranteed round of a half, Scrim Mod should announce:

\*\*\* LAST ROUND OF THE HALF — BUY OUT \*\*\*

For an MR12 regulation half, this occurs before round 12 of the half.

The second half may end early because of match point, so the plugin should not necessarily announce a "last round" unless the upcoming round is guaranteed to be the final scheduled round.

The plugin may additionally announce:

\*\*\* MATCH POINT: Team A \*\*\*

when appropriate.

## **Halftime**

After the configured number of first-half rounds has been completed, Scrim Mod should automatically switch sides.

The logical teams remain unchanged.

For example:

Before halftime:  
Team A \-\> T  
Team B \-\> CT

After halftime:  
Team A \-\> CT  
Team B \-\> T

Internal match scores should NOT be swapped.

If Team A leads 7-5:

Team A: 7  
Team B: 5

remains true after the side switch.

If necessary, Scrim Mod should update the game's CT/T scoreboard display to reflect the new side mappings.

After teams are switched, Scrim Mod should run LO3 again.

## **Second Half**

The second half continues using the existing match score.

For regulation using MR12:

Rounds per half: 12  
Rounds needed to win: 13

The match ends as soon as one logical team reaches:

Rounds Per Half \+ 1

Therefore:

13 wins \-\> Match Win

If regulation completes tied:

12-12

the match proceeds to overtime.

## **Overtime**

Overtime should use separate overtime-period scoring while continuing to maintain the overall match score. We should execute calot.cfg to reflect any extra settings for overtime.

For an MR3 overtime:

Overtime rounds per half: 3

Each overtime period consists of:

3 rounds  
Side switch  
3 rounds

Each overtime period begins with its own score:

OT Team A: 0  
OT Team B: 0

The overall match score continues normally.

For example, regulation may end:

Overall:  
Team A 12  
Team B 12

During overtime:

OT:  
Team A 2  
Team B 1

Overall:  
Team A 14  
Team B 13

## **Overtime Halftime**

After the configured number of overtime first-half rounds, teams automatically switch sides.

The current overtime-period score should remain associated with the logical teams.

The configured overtime starting money should be applied again before the second overtime half.

LO3 should then be executed.

## **Winning Overtime**

For MR3 overtime, a team wins the overtime period when it reaches:

Overtime Rounds \+ 1

Therefore:

4 overtime round wins \-\> Match Win

If all six overtime rounds are played and the overtime period ends:

3-3

the overall match remains tied and another overtime period begins.

For example:

Regulation:  
12-12

OT \#1:  
3-3

Overall:  
15-15

OT \#2:  
0-0

The process repeats until one team wins an overtime period.

## **Match Completion**

Once one team wins, Scrim Mod should announce the result.

For example:

\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*  
Team A wins\!  
Final Score: 16-13  
\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*

The plugin should then:

* Enter `MatchComplete`  
* Remove match-specific restrictions  
* Execute `pregame.cfg`  
* Clear active/live match behavior

The completed match state may remain available temporarily through `scrim_status` for debugging or reporting, but beginning a new match or disabling Scrim Mod should completely reset it.

## **Admin Recovery Commands**

Scrim Mod should provide administrative recovery commands even if they are not normally needed during a match.

At minimum:

scrim\_status

scrim\_pause  
scrim\_resume

scrim\_restart\_phase  
scrim\_setphase \<phase\>

scrim\_setscore \<teamA\> \<teamB\>

scrim\_swap\_sides

scrim\_forcepick \<captain\> \<player\>

scrim\_knife\_restart  
scrim\_knife\_winner \<player\>

scrim\_add \<player\>  
scrim\_remove \<player\>

scrim\_abort

Exact command names may change during implementation.

### **scrim\_status**

`scrim_status` should provide enough information to reconstruct what Scrim Mod believes is happening.

For example:

Scrim Mod: ENABLED  
Phase: Draft

Team A Captain: dook  
Team B Captain: disgrace

Team A Side: CT  
Team B Side: T

Knife Winner: disgrace  
First Pick: Team A

Team A:  
  dook  
  efegege

Team B:  
  disgrace

Available:  
  garetjax  
  foo  
  bar

Draft Type: Snake  
Next Pick: Team B  
Picks Remaining This Turn: 2

Score:  
Team A 0  
Team B 0

This command should be considered an important debugging and recovery tool.

## **Settings**

### **Enabled**

0 / 1

Controls whether Scrim Mod is active.

When disabled, Scrim Mod should perform no match behavior.

### **Draft Type**

AB  
Snake

`AB`:

A B A B A B...

`Snake`:

A B B A A B B A A...

### **Rounds**

Rounds in each regulation half.

Default:

12

This results in MR12:

12 rounds first half  
First to 13 wins  
12-12 \-\> overtime

### **Overtime Rounds**

Rounds in each overtime half.

Default:

3

This results in MR3 overtime:

3 rounds  
switch  
3 rounds

First to 4 OT wins  
3-3 \-\> another overtime

### **Overtime Start Money**

Starting money during overtime.

Default:

10000

Scrim Mod's configured overtime starting money should override conflicting settings from other configs.

## **Suggested Internal Components**

The implementation should ideally keep major responsibilities separated.

Conceptually:

ScrimPlugin  
    |  
    \+-- MatchState  
    |     Phase  
    |     Captains  
    |     Players  
    |     Logical Teams  
    |     Side Assignments  
    |     Match Score  
    |     Period Score  
    |     Draft State  
    |     Ready State  
    |  
    \+-- StateMachine  
    |     EnterState()  
    |     ExitState()  
    |     RewindTo()  
    |  
    \+-- PlayerManager  
    |     Steam ID Tracking  
    |     Reconnect Handling  
    |     Team Enforcement  
    |  
    \+-- DraftManager  
    |     Eligible Pool  
    |     Draft Order  
    |     Picks  
    |  
    \+-- MatchManager  
    |     Round Results  
    |     Score Tracking  
    |     Halftime  
    |     Overtime  
    |     Match Completion  
    |  
    \+-- UI  
    |     Menus  
    |     Chat Commands  
    |  
    \+-- ServerConfig  
          Pregame  
          LO3  
          Match Cvars  
          Overtime Cvars

The central architectural rule should be:

> **Scrim Mod's internal match state is authoritative. The Counter-Strike server is a representation of that state.**

Side assignments, player teams, scores, menus, and server configuration should be reconciled against that state rather than used to infer it.

## **Supported Platform**

Scrim Mod targets a Linux Counter-Strike 1.6 server running:

* ReHLDS
* ReGameDLL_CS
* Metamod-R

The deployable artifact is a 32-bit Linux shared object (`scrimmod_mm_i386.so`)
compiled with GCC. AppleClang may be used to build and test the platform-independent
core during macOS development, but it is not the production plugin toolchain.

Support for the original HLDS/GameDLL, Windows server binaries, and macOS server
binaries is outside the initial scope. Dependency revisions should be pinned to
known-good versions before the plugin adapter is implemented.

Development may occur on macOS. The platform-independent core and its unit tests
must build and run there, while the actual plugin is compiled and integration-tested
on Linux.

## **Architecture Contract**

The match engine should be a platform-independent C++ library with no engine,
Metamod, ReHLDS, or ReGameDLL calls. It owns match state, validates commands, performs
state transitions, and produces explicit effects describing what the server adapter
must do.

A thin Linux plugin adapter should:

* Register and receive Metamod/ReGameDLL hooks
* Convert server callbacks into typed core events
* Apply effects produced by the core
* Execute configuration files and server commands
* Reconcile connected players, sides, weapons, menus, and messages with core state

Engine callbacks must not directly mutate match state. Server state must not be used
to reconstruct authoritative logical match state.

All state entry and reconciliation operations should be idempotent. Duplicate or
ambiguous round events must never silently advance scoring.

## **State Lifetime and Failure Policy**

Player assignments survive disconnect/reconnect during an active plugin instance by
Steam ID. Persistent recovery across a server process restart or plugin unload/reload
is not required initially. Startup, plugin reload, and unrecoverable initialization
failure must leave the server in a disabled, reset, pregame-safe condition.

Behavior across map changes will be implemented only after an explicit lifecycle
test is available. Until then, a map change should safely abort and reset the active
match rather than attempt a partial recovery.

When an external event is ambiguous, Scrim Mod should pause, reconcile, replay the
affected phase, or require admin recovery. It must not guess and advance the match.

## **Testing Contract**

Core behavior should be covered by unit tests that do not require a game server.
Every state transition, rewind boundary, draft sequence, scoring boundary, overtime
cycle, disconnect/reconnect path, and fixed state-machine regression should have a
test.

Linux integration tests should eventually verify hook behavior, duplicate-event
suppression, config/restart sequencing, team enforcement, menus, and plugin
load/unload behavior against the pinned server stack.
