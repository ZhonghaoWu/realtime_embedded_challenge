# realtime_embedded_challenge

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
