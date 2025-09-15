\#zhorse\_asi<BR>

EQA Players now have more options with Horse QoL in game. <BR>
(thanks zeal and PQ teams)<BR>

Horse QoL Support<BR>
1\. Enables option for Horses to work with Old Models the same as they do with Luclin Models<BR>
2\. Mounted Inertia Fix - Matches the Horse QoL now used in PQ<BR>
   -Immediate deceleration, horses now stop on a dime<BR>
   -Accelerate immediately up to at least running speed but then slowly accelerate to max.<BR>
   -First five seconds same distance covered as original function.<BR>
   -Max speed horse takes approx. ~9 seconds to reach max speed.<BR>
3\. Mounted Ducking fix - enables ducking while mounted, allows player to interrupt spells while mounted<BR>
4\. Mounted Z-coordinate Fix - while Mounted levitating allows server and other players to see Mount in the air<BR>
5\. Mounted Horse tilt Fix - while Mounted levitating so horse tilt doesn't contour to ground when in air<BR>
6\. Mounted Z-axis Downward Control - while Mounted levitating allows players to move downward<BR>
7\. Prevent horse super speed in water - blocks players from exploiting water bug for super speed horse<BR><BR>
AK Horse Mechanic for both Luclin and Old models<BR>
As an alternative to above Horse QoL players also have option to use the original AK horse bug<BR>
8\. AK Horse mechanic bug now works with both Luclin Models and Old Models if desired<BR>
9\. If AK Horse mechanic enabled: Prevent invisible horse collision<BR>
<BR>
Tips on how to use horse QoL: There are three settings in your eqclient.ini file that provide mounted options to the player.<BR>
<BR>
To enable Horse QoL with Old Models<BR>
AllLuclinPcModelsOff=TRUE<BR>
UseLuclinElementals=TRUE<BR>
UseLuclinHorses=TRUE<BR>
<BR>
To enable Horse QoL with Luclin Models<BR>
AllLuclinPcModelsOff=FALSE<BR>
UseLuclinElementals=TRUE<BR>
UseLuclinHorses=TRUE<BR>
<BR>
To enable AK Horse mechanic with Old Models<BR>
AllLuclinPcModelsOff=TRUE<BR>
UseLuclinElementals=FALSE<BR>
UseLuclinHorses=FALSE<BR>
<BR>
To enable AK Horse mechanic with Luclin Models<BR>
AllLuclinPcModelsOff=FALSE<BR>
UseLuclinElementals=TRUE<BR>
UseLuclinHorses=FALSE<BR>
<BR>
<BR>
Credit to Adonis - initial horse QoL code work on zeal, passed from zeal to DLL via Viskar, streamlined DLL code to zhorse.asi<BR>
Credit to Viskar - translated Adonis work to eqgame.dll, bug fixed, professionalized, polished, and iterated to current state<BR>
Credit to Sherra - wrote Mounted Inertia code to Ailia specs, suggested Fixes to included DLL rather than zeal so everyone benefits<BR>
Credit to Ailia - providing specs that balanced mount fixes while mitigating existing/possible mount exploits<BR>
Credit to solar - deep client knowledge, server operator knowledge, and supplied support while others tested<BR>
<BR>
Thanks everyone for collaborative efforts in coding, testing, and client knowledge.

