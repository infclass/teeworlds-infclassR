# TeeWorlds InfClassR
Slightly modified version of original [InfClass by necropotame](https://github.com/necropotame/teeworlds-infclass).
## Additional dependencies
[GeoLite2++](https://www.ccoderun.ca/GeoLite2++/api/) is used for IP geolocation
```bash
sudo apt install libmaxminddb-dev
```
## Maps
You can create new maps with [TeeUniverse](https://github.com/teeuniverse/teeuniverse)

### Map commands
You can type these commands inside the rcon console (F2) or 
you can also put them into a file called autoexec.cfg which should be next to your server binary.

With ```sv_maprotation``` you can define what maps will be played in which order, for example:
```sv_maprotation "infc_skull infc_warehouse infc_damascus"```
With ```sv_rounds_per_map``` you can define how many rounds each map should be played before changing to the next map.

You can change maps by using these commands: ```skip_map```, ```sv_map mapname```, ```change_map mapname```
```skip_map``` will change the map to the next in the rotation.
```sv_map infc_skull``` will instantly change map to a map called infc_skull.
```change_map infc_skull``` will show the score of the current round and then change map.

You can create map votes like this:
```add_vote "infc_skull" change_map infc_skull```
```add_vote "skip this map" skip_map```

More commands:
```inf_min_rounds_map_vote``` how many rounds should be played on a map before players can start a map vote.
```inf_min_player_percent_map_vote``` how many percent of all players need to vote for a map in order to start a map vote.
```inf_min_player_number_map_vote``` how many players need to vote for a map in order to start a map vote.
If either ```inf_min_player_percent_map_vote``` or ```inf_min_player_number_map_vote``` becomes true, a map vote will start.
By default these features are deactivated.
