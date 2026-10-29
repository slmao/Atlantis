// Plan 0068 M2: the model works on the authoring document, whose command and event
// records share names with the Runtime World's in Atlantis.Gameplay; these aliases
// pick the authoring ones for the whole library.
global using Command = Atlantis.Gameplay.Authoring.Command;
global using CommandFailure = Atlantis.Gameplay.Authoring.CommandFailure;
global using Event = Atlantis.Gameplay.Authoring.Event;
global using EventFilter = Atlantis.Gameplay.Authoring.EventFilter;
global using SetProperty = Atlantis.Gameplay.Authoring.SetProperty;
global using AddComponent = Atlantis.Gameplay.Authoring.AddComponent;
global using RemoveComponent = Atlantis.Gameplay.Authoring.RemoveComponent;
global using PropertyChanged = Atlantis.Gameplay.Authoring.PropertyChanged;
global using ComponentAdded = Atlantis.Gameplay.Authoring.ComponentAdded;
global using ComponentRemoved = Atlantis.Gameplay.Authoring.ComponentRemoved;
