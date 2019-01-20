# TeeWorlds InfClassR
Slightly modified version of original [InfClass by necropotame](https://github.com/necropotame/teeworlds-infclass).
## Additional dependencies
[GeoLite2++](https://www.ccoderun.ca/GeoLite2++/api/) is used for IP geolocation
```bash
sudo apt install libmaxminddb-dev
```
## Config variables
The variables are defined here:<br/>
[config_variables.h](src/engine/shared/config_variables.h)<br/>
[variable.h](src/game/variables.h)<br/>
You can type these commands inside the rcon console (F2) or <br/>
you can also put them into a file called autoexec.cfg which should be next to your server binary.

## Maps
You can create new maps with [TeeUniverse](https://github.com/teeuniverse/teeuniverse)

### .mapinfo
You can create a map info file with the same name as the map and put it next to it. (e.g. "infc_skull.mapinfo")<br/>
Inside this file you can define 2 variables:<br/>
```timelimit x```<br/>
x is the number of minutes a round should take on this map.<br/>
```minplayers x```<br/>
x is the minimum number of players this map should be played on, this has an impact on the map rotation.<br/>
If there are not enough players the server will skip this map. (But you can still vote and play on it)

### Map commands

With ```sv_maprotation``` you can define what maps will be played in which order, for example:<br/>
```sv_maprotation "infc_skull infc_warehouse infc_damascus"```<br/>
With ```sv_rounds_per_map``` you can define how many rounds each map should be played before changing to the next map.<br/>
If ```inf_maprotation_random``` is set to 1 than a random map will be chosen after a map finished.

You can change maps by using these commands: ```skip_map```, ```sv_map mapname```, ```change_map mapname```<br/>
```skip_map``` will change the map to the next in the rotation.<br/>
```sv_map infc_skull``` will instantly change map to a map called infc_skull.<br/>
```change_map infc_skull``` will show the score of the current round and then change map.

You can create map votes like this:<br/>
```add_vote "infc_skull" change_map infc_skull```<br/>
```add_vote "skip this map" skip_map```<br/>

More commands:<br/>
```inf_min_rounds_map_vote``` how many rounds should be played on a map before players can start a map vote.<br/>
```inf_min_player_percent_map_vote``` how many percent of all players need to vote for a map in order to start a map vote.<br/>
```inf_min_player_number_map_vote``` how many players need to vote for a map in order to start a map vote.<br/>
If either ```inf_min_player_percent_map_vote``` or ```inf_min_player_number_map_vote``` becomes true, a map vote will start.<br/>
By default these features are deactivated.
