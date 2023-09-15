# realtime_embedded_challenge
ECE-GY 6483 Final Project Demo
Motion Detection with Gyroscope
Group 42: Zhiyu Ning, Zhiheng Li, Zhonghao Wu, Zhongyi Ye
Objective Overview
● Use the data collected from an gyroscope (the one integrated on STM32F429I) to record a hand movement 
sequence as a password to "unlock" a resource.
● Save the recorded sequence on the microcontroller (STM32F429I), using a “Record Key” feature.
User then must replicate the password within sufficient tolerances to unlock the resource.
● Read 60 sigmoid-processed samples for password and inputs from user.
● Movement Extraction & Data Preparation for DTW: save the full length data array into double vectors. 
● Use Dynamic Time Warping (DTW) algorithm to calculate the similarity between the recorded password 
and the user entry.
● A successful unlock is indicated by a green LED blinking 3 times, if the answer is below 90% will be indicated 
by a red LED blinking 3 times. 
● Implement a GUI view that display the stage for current operation: Recording or not recording.
General Design
● Press on-screen GUI button or on-board user button to start recording a password
● Subsequent presses would allow user to enter an input (password entry attempt)
● Blink green LED if correct, red if wrong
● Show “UNLOCKED” on screen if correct, “WRONG PASSWORD” if wrong
● Press RESET button to reset password
Record Key
To record a movement, the user should press the on-board blue user button 
OR press the "RECORD" GUI button on touch screen, both of which will 
start the recording process. 
The system then records the hand movement sequence (angular speed 
readings) through the gyroscope and saves it to the microcontroller RAM in 
vector form.
GUI: Password Setup
GUI: Recording Data
GUI: Password Recorded, 
Waiting for User Entry
Unlock Resource
To unlock the resource, the user should press the "UNLOCK" button on touch screen or re-press the blue 
user button on board to start the unlocking process. 
The system then records the user’s movement into another set of vectors. Once the user input sequence 
is recorded, the system compares it with the recorded password sequence using DTW. 
If the sequence matches within a certain tolerance, the GUI would alert that “UNLOCKED” on the screen 
and blink its green LED. 
If resources unlock unsuccessful, the GUI would alert that “WRONG PASSWORD” on the screen and 
blink its red LED.
GUI: Password 
Correct - 
“UNLOCKED”
GUI: Wrong Password
Data Processing
● Read real-time angular speed from gyroscope at fixed sampling rate (20Hz)
● Pass raw reading through Sigmoid function
● Use c++ vector to hold data samples (one vector for each axis x, y, z)
● Compare vectors of angular speed readings (password and user entry) through the Dynamic Time
Warping algorithm
Data Processing: DTW (Dynamic Time Warping)
● Measures the similarity between two temporal signals.
● Returns the smallest-possible summation of Euclidean 
Distances between the two signals’ sample points.
● Smaller distance -> more similar; larger distance -> less 
similar
● Commonly used for voice recognition.
Function imported from a public Git repo: https://github.com/cjekel/DTW_cpp/tree/master
Demonstration of Dynamic Time Warping with 
Euclidean distances between two signals
Design Demonstration (Demo Video on Youtube)
We have recorded a video demonstration of the unlock sequence, which shows that the recording process is 
repeatable and robust. 
We have tested the device with movements of different angles, directions, speeds, and delays, and it has been 
able to recognize the correct unlock sequence consistently. 
The video demonstration shows how the design works and how the user can perform the hand gestures to 
unlock the phone easily.
(YouTube link goes there )
https://youtu.be/4LeJFdSLmkI
