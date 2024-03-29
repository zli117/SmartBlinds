# RESTful Server For Blinds
![image](image.jpg)

 * Get System Info `GET`
   * Endpoint: `/system_info`
   * Response:
   ```
   {
      "version": str  // Version string
      "cores": int  // Number of cores
   }
   ```

 * Get Current Status `GET`
   * Endpoint: `/status`
   * Response:
   ```
   {
      "max_steps": int  // Total number of steps possible
      "current_steps": int   // Current step number
   }
   ```

 * Unsafe Move By Steps `PUT`
   * Endpoint: `/unsafe_move`
   * Request:
   ```
   {
      "steps": int  // Move this number of steps. Can be negative.
   }
   ```

 * Move to Location `PUT`
   * Endpoint: `/move`
   * Request:
   ```
   {
      "fraction": double  // Move to fraction of the max_steps.
                          // In range [0, 1].
   }
   ```

 * Reset State `PUT`
   * Endpoint: `/reset_state`