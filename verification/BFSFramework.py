
from collections import deque
from typing import TypeVar, Generic, Set, List, Callable, Dict, Any, Optional

# Generic type for state representation
S = TypeVar('S')

class BFSFramework(Generic[S]):
    """
    A generic framework for breadth-first search.
    
    Args:
        is_goal_state: Function that determines if a state is the goal state.
        get_next_states: Function that returns possible next states from the current state.
        state_to_hashable: Function that converts a state to a hashable representation for visited tracking.
    """
    
    def __init__(
        self,
        is_goal_state: Callable[[S], bool],
        get_next_states: Callable[[S], List[S]],
        state_to_hashable: Callable[[S], Any] = None
    ):
        self.is_goal_state = is_goal_state
        self.get_next_states = get_next_states
        self.state_to_hashable = state_to_hashable or (lambda x: x)  # Default to identity function
    
    def search(self, initial_state: S) -> Optional[List[S]]:
        """
        Perform breadth-first search from initial_state to find goal state.
        
        Args:
            initial_state: The starting state for the search.
            
        Returns:
            A path from initial_state to goal state, or None if no path exists.
        """
        if self.is_goal_state(initial_state):
            return [initial_state]
        
        # Queue of states to explore
        queue = deque([(initial_state, [initial_state])])
        
        # Set of visited states (using hashable representation)
        visited = {self.state_to_hashable(initial_state)}
        
        while queue:
            current_state, path = queue.popleft()
            
            for next_state in self.get_next_states(current_state):
                hashable_next = self.state_to_hashable(next_state)
                
                if hashable_next not in visited:
                    visited.add(hashable_next)
                    new_path = path + [next_state]
                    
                    if self.is_goal_state(next_state):
                        return new_path
                    
                    queue.append((next_state, new_path))
        
        # No path found
        return None
    
    def search_all_paths(self, initial_state: S, max_depth: int = None) -> List[List[S]]:
        """
        Find all paths from initial_state to goal states within max_depth.
        
        Args:
            initial_state: The starting state for the search.
            max_depth: Maximum search depth (None for unlimited).
            
        Returns:
            List of all paths from initial_state to goal states.
        """
        all_paths = []
        
        if self.is_goal_state(initial_state):
            all_paths.append([initial_state])
        
        # Queue of states to explore
        queue = deque([(initial_state, [initial_state])])
        
        # Set of visited states (using hashable representation)
        visited = {self.state_to_hashable(initial_state)}
        
        while queue:
            current_state, path = queue.popleft()
            
            # Check if we've reached max depth
            if max_depth is not None and len(path) > max_depth:
                continue
            
            for next_state in self.get_next_states(current_state):
                new_path = path + [next_state]
                
                if self.is_goal_state(next_state):
                    all_paths.append(new_path)
                
                hashable_next = self.state_to_hashable(next_state)
                if hashable_next not in visited:
                    visited.add(hashable_next)
                    queue.append((next_state, new_path))
        
        return all_paths
    
    def search_with_cost(self, initial_state: S, cost_func: Callable[[S, S], float]) -> Optional[Dict]:
        """
        Perform breadth-first search considering edge costs.
        
        Args:
            initial_state: The starting state for the search.
            cost_func: Function to calculate cost between two states.
            
        Returns:
            Dictionary with path and total cost, or None if no path exists.
        """
        if self.is_goal_state(initial_state):
            return {"path": [initial_state], "cost": 0}
        
        # Queue of states to explore
        queue = deque([(initial_state, [initial_state], 0)])
        
        # Set of visited states (using hashable representation)
        visited = {self.state_to_hashable(initial_state)}
        
        while queue:
            current_state, path, total_cost = queue.popleft()
            
            for next_state in self.get_next_states(current_state):
                hashable_next = self.state_to_hashable(next_state)
                step_cost = cost_func(current_state, next_state)
                new_cost = total_cost + step_cost
                new_path = path + [next_state]
                
                if self.is_goal_state(next_state):
                    return {"path": new_path, "cost": new_cost}
                
                if hashable_next not in visited:
                    visited.add(hashable_next)
                    queue.append((next_state, new_path, new_cost))
        
        # No path found
        return None
