library ieee;
use ieee.std_logic_1164.all;
entity broken is port (a : in std_logic; y : out std_logic); end entity;
architecture rtl of broken is begin
    y <= a and ;
end architecture;
