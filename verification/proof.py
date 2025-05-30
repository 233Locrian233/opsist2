from typing import TypeVar, Generic, Set, List, Callable, Dict, Any, Optional
from BFSFramework import BFSFramework

server_states = ["Wait Semaphore", "Modify State", "Release Update", "Release Semaphore", "Sleep Time"]
client_states = ["Wait Update", "Wait Semaphore", "Modify View", "Release Semaphore"]

class Prog_State:
    """
        Representa um estado possivel do ciclo
    """

    def __init__(self, server_state_idx : int, client_state_idx : int):
        self.server_state_idx = server_state_idx
        self.client_state_idx = client_state_idx
    
    def __eq__(self, other):
        if not isinstance(self, other):
            return False
        else:
            return self.client_state == other.client_state 
    
    def __repr__(self):
        return "(" + server_states[self.server_state_idx] + ", " + client_states[self.client_state_idx] + ")"
    
    def state(self):
        return (self.server_state_idx, self.client_state_idx)
    
    def next(self):
        return Prog_State((self.server_state_idx + 1) % len(server_states), (self.client_state_idx + 1) % len(client_states))
    
    def nextServer(self):
        return Prog_State((self.server_state_idx + 1) % len(server_states), self.client_state_idx)
    
    def nextClient(self):
        return Prog_State(self.server_state_idx, (self.client_state_idx + 1) % len(client_states))
    

def is_goal_state(s : Prog_State):
    
    """
        Se o algoritmo chegar a este estado, então o deadlock é possivel, e a nossa hipótese estará errada 
    """
    
    (s, c) = s.state()
    return "Wait Semaphore" in server_states[s] and "Wait Semaphore" in client_states[c]

def get_next_states(state : Prog_State) -> List[Prog_State]:
    
    """
        Next state, given s
    """

    if(is_goal_state(state)):   # deadlock
        return []
    
    (s, c) = state.state() # (server_state_idx : int, client_state_idx : int)

    if is_goal_state(state):
        return []   # deadlock
        
    """
        only serverstep
        only clientstep
        only globalstep
        all steps
    """

    if c == 0 and s != 2 or c == 1 and s != 3:
        return [state.nextServer()]
    if s == 0 and c != 3:
        return [state.nextClient()]
    if s == 0 and c == 3 or c == 0 and s == 2 or c == 1 and s == 3:
        return [state.next()]
    else:
        return [state.next(), state.nextClient(), state.nextServer()]


        
if __name__ == "__main__":
    bfs = BFSFramework(is_goal_state=is_goal_state, get_next_states=get_next_states, state_to_hashable=(lambda x : x.__repr__()))
    
    for i, _ in enumerate(server_states):
        print(bfs.search(Prog_State(i, 0)))
